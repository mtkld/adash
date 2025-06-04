/***********************************************************************
 *  statlog – simple TUI project time-logger
 *  two-pane version – 2025-05-31
 *
 *  NEW
 *  ──────────────────────────────────────────────────────────────────
 *  • Two modes: LIST view and DATA view (per-project).
 *  • Arrow/j-k navigation & live highlight in list view.
 *  • Press ↵ to open a project from the list (sets it active).
 *  • `c` in data view prompts for a new comment.
 *  • Help bars updated to reflect new keys.
 ***********************************************************************/
#define _XOPEN_SOURCE 700
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <locale.h>
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                       */

#define MAX_COMMENTS 1024
#define MAX_LINE 1024
#define MAX_PROJECTS 512

/* ------------------------------------------------------------------ */
/* Types                                                               */

/* ===== add a preview to each entry ==== */
typedef struct
{
      char id[ 128 ];
      char last_action[ 32 ];
      char timestamp[ 32 ];
      char preview[ 64 ]; /* ← NEW: last comment, truncated */
} ProjectInfo;

typedef struct
{
      char timestamp[ 32 ];
      char message[ MAX_LINE ];
} Comment;

/* ------------------------------------------------------------------ */
/* Forward decls                                                       */

static void trim( char *s );
static const char *expand_home( const char *path );

static void load_active_project( void );
static void load_comments( void );

static void write_log( const char *action, const char *comment );

static int compare_projects( const void *a, const void *b );

static void draw_data_view( void );
static void data_view_handle( int key );

static void list_view( void ); /* full interactive loop */
static int is_status_match( const char *status, const char *filter );
static int build_project_list( ProjectInfo *out, int *visible, int sel_filter,
                               int *vis_count );
static void create_new_project( void );
static void add_comment( void );

static void delete_comment( void );
static void list_all_comments( void );
static int compute_total_minutes( void );
/* ------------------------------------------------------------------ */
/* Globals                                                             */

static char BASE_DIR[ PATH_MAX ]    = "";
static char STATE_DIR[ PATH_MAX ]   = "";
static char DATA_DIR[ PATH_MAX ]    = "";
static char ARCHIVE_DIR[ PATH_MAX ] = "";

static Comment comments[ MAX_COMMENTS ];
static int comment_count = 0;
static int index_pos     = 0;

static char active_id[ 128 ] = "";

static const char *RUNNING_FILE = "running";
static const char *LOCK_FILE    = "checkedin";

/* filters for list view */
enum
{
      FLT_ALL,
      FLT_FINISH,
      FLT_CANCEL,
      FLT_STARTED
};

/* current UI mode */
enum
{
      MODE_DATA,
      MODE_LIST,
      MODE_QUIT
};
static int mode = MODE_LIST; /* default: list view */

// General helper
//
static void safe_snprintf( char *buf, size_t size, const char *fmt, ... )
{
      if ( size == 0 || buf == NULL || fmt == NULL )
            return;

      va_list args;
      va_start( args, fmt );
      vsnprintf( buf, size, fmt, args );
      va_end( args );

      // Null-terminate just in case (vsafe_snprintf always does, but safe to
      // reinforce)
      buf[ size - 1 ] = '\0';
}

/* -------------------------------------------------- */
/*  Check-in lock helpers                             */
/* -------------------------------------------------- */
/* ----------------------------------------------------
   Exclusive-check-in helpers (use LOCK_FILE)
   ---------------------------------------------------- */
static int lock_read( char *buf, size_t n )
{
      char p[ PATH_MAX ];
      safe_snprintf( p, sizeof p, "%s/%s", STATE_DIR, LOCK_FILE );
      FILE *f = fopen( p, "r" );
      if ( !f )
      {
            *buf = 0;
            return 0;
      }
      if ( fgets( buf, n, f ) )
            trim( buf );
      fclose( f );
      return 1;
}
static void lock_write( const char *id ) /* id==NULL → clear */
{
      char p[ PATH_MAX ];
      safe_snprintf( p, sizeof p, "%s/%s", STATE_DIR, LOCK_FILE );
      if ( id && *id )
      {
            FILE *f = fopen( p, "w" );
            if ( f )
            {
                  fputs( id, f );
                  fputc( '\n', f );
                  fclose( f );
            }
      }
      else
            unlink( p );
}

/* ------------------------------------------------------------------ */
/* UI helpers                                                         */
/* ------------------------------------------------------------------ */

static void flash_msg( int row, const char *fmt, ... )
{
      /* row == -1 → bottom-2 */
      if ( row < 0 )
            row = LINES - 3;

      char buf[ 256 ];
      va_list ap;
      va_start( ap, fmt );
      vsnprintf( buf, sizeof buf, fmt, ap );
      va_end( ap );

      attron( A_BOLD );
      mvprintw( row, 0, "%-*s", COLS, "" ); /* clear line        */
      mvprintw( row, 0, "%s", buf );        /* show message      */
      attroff( A_BOLD );
      refresh();

      beep();       /* short audible/visual bell        */
      napms( 900 ); /* keep visible ~0.9 s               */

      /* clear again so the UI redraw looks clean            */
      mvprintw( row, 0, "%-*s", COLS, "" );
      refresh();
}
/* ------------------------------------------------------------------ */
/* Helpers                                                             */

/* Accept “…:SS” (local) and “…:SSZ” (UTC) */
static time_t parse_iso_ts( const char *s )
{
      struct tm t = { 0 };

      /* 1. try “…Z” (UTC) */
      if ( strptime( s, "%Y-%m-%dT%H:%M:%SZ", &t ) )
      {
#ifdef _GNU_SOURCE               /* timegm() is a GNU / BSD extension */
            return timegm( &t ); /* treat string as UTC */
#else
            t.tm_isdst = 0;
            return mktime(
                &t ); /* falls back to local TZ if timegm() unavailable */
#endif
      }

      /* 2. try old “…:SS” (local) */
      if ( strptime( s, "%Y-%m-%dT%H:%M:%S", &t ) )
      {
            t.tm_isdst = -1;
            return mktime( &t ); /* local time → seconds since epoch */
      }

      return (time_t)-1; /* parse failed */
}
static void draw_status_bar( void )
{
      char locked[ 128 ] = "";
      lock_read( locked, sizeof locked );

      move( 0, 0 );
      clrtoeol();
      attron( A_BOLD );
      if ( *locked )
            mvprintw( 0, 0, "Checked-in to      🟢 %s", locked );
      else
            mvprintw( 0, 0, "No project currently checked-in" );
      attroff( A_BOLD );
}
static int has_checkin( void )
{
      char locked[ 128 ] = "";
      lock_read( locked, sizeof locked );
      return locked[ 0 ] != '\0';
}
static int is_the_checked_in_project( void )
{
      char locked[ 128 ] = "";
      lock_read( locked, sizeof locked );
      return *active_id && strcmp( locked, active_id ) == 0;
}

static void save_active_project( const char *id ) /* id may be NULL */
{
      char path[ PATH_MAX ];
      safe_snprintf( path, sizeof path, "%s/%s", STATE_DIR, RUNNING_FILE );

      if ( id && *id )
      { /* write / overwrite */
            FILE *f = fopen( path, "w" );
            if ( f )
            {
                  fputs( id, f );
                  fputc( '\n', f );
                  fclose( f );
            }
      }
      else
      { /* clear */
            unlink( path );
      }
}

/* ========= 1. helper: ensure an ID is well-formed ============ */
// Accept anything that is a valid filename
static int valid_id( const char *s )
{
      if ( !*s )
            return 0;

      for ( ; *s; ++s )
      {
            unsigned char c = (unsigned char)*s;

            if ( c == '/' || c == 0 ) // disallow path separator and null byte
                  return 0;
            if ( iscntrl( c ) ) // disallow control characters
                  return 0;
      }

      return 1;
}

static int compute_total_minutes( void )
{
      if ( !*active_id )
            return 0;

      char path[ PATH_MAX ];
      safe_snprintf( path, sizeof( path ), "%s/%s.log", DATA_DIR, active_id );

      FILE *f = fopen( path, "r" );
      if ( !f )
            return 0;

      char line[ MAX_LINE ];
      time_t checkin_time = 0;
      int total_minutes   = 0;

      while ( fgets( line, sizeof( line ), f ) )
      {
            char *ts  = strtok( line, "\t" );
            char *act = strtok( NULL, "\t" );
            if ( !ts || !act )
                  continue;

            time_t t_sec = parse_iso_ts( ts );
            if ( t_sec == (time_t)-1 )
                  continue; /* un-parsable timestamp */

            if ( strcmp( act, "checkin" ) == 0 )
                  checkin_time = t_sec;
            else if ( strcmp( act, "checkout" ) == 0 && checkin_time )
            {
                  total_minutes += (int)( ( t_sec - checkin_time ) / 60 );
                  checkin_time = 0;
            }
      }

      fclose( f );
      return total_minutes;
}

static void list_all_comments( void )
{
      if ( comment_count == 0 )
            return;

      int top = 0;
      int ch;

      while ( 1 )
      {
            clear();
            mvprintw( 0, 0, "==== All Comments for %s ====", active_id );
            mvprintw( 1, 0, "Use j/k or ↑/↓ to scroll, q to return" );

            int max_rows = LINES - 4; // Leave space for header/footer
            int shown    = 0;

            for ( int i = top; i < comment_count && shown < max_rows;
                  ++i, ++shown )
            {
                  mvprintw( shown + 3, 0, "[%d] %s", i + 1,
                            comments[ i ].timestamp );
                  mvprintw( shown + 3, 25, "%s", comments[ i ].message );
            }

            refresh();
            ch = getch();

            if ( ch == 'q' )
                  break;
            if ( ch == 'j' || ch == KEY_DOWN )
            {
                  if ( top < comment_count - 1 )
                        ++top;
            }
            if ( ch == 'k' || ch == KEY_UP )
            {
                  if ( top > 0 )
                        --top;
            }
      }
}
/* Delete the comment at index_pos from the active project's log  */
// TODO: This function fails silently...
static void delete_comment( void )
{
      if ( !*active_id || comment_count == 0 || index_pos < 0 ||
           index_pos >= comment_count )
            return;

      const char *ts  = comments[ index_pos ].timestamp;
      const char *msg = comments[ index_pos ].message;

      char src[ PATH_MAX ], tmp[ PATH_MAX ];
      safe_snprintf( src, sizeof( src ), "%s/%s.log", DATA_DIR, active_id );
      safe_snprintf( tmp, sizeof( tmp ), "%s/%s.tmp", DATA_DIR, active_id );

      FILE *in  = fopen( src, "r" );
      FILE *out = in ? fopen( tmp, "w" ) : NULL;
      if ( !in || !out )
      {
            if ( in )
                  fclose( in );
            if ( out )
                  fclose( out );
            return;
      }

      char line[ MAX_LINE ];
      while ( fgets( line, sizeof( line ), in ) )
      {
            if ( !strstr( line, "\tcomment\t" ) )
            { /* not a comment → copy verbatim  */
                  fputs( line, out );
                  continue;
            }

            /* parse the line to see if it matches the one we want to drop */
            char copy[ MAX_LINE ];
            strcpy( copy, line );
            char *lts = strtok( copy, "\t" );
            strtok( NULL, "\t" ); /* "comment" */
            char *lmsg = strtok( NULL, "\n" );

            if ( !( lts && lmsg && strcmp( lts, ts ) == 0 &&
                    strcmp( lmsg, msg ) == 0 ) )
            { /* keep everything except the exact match */
                  fputs( line, out );
            }
      }

      fclose( in );
      fclose( out );
      rename( tmp, src ); /* atomically replace */
}
// static char *now_iso( void )
// {
//       static char buf[ 32 ];
//       time_t t = time( NULL );
//       strftime( buf, sizeof( buf ), "%Y-%m-%dT%H:%M:%S", localtime( &t ) );
//       return buf;
// }

static char *now_iso( void )
{
      static char buf[ 32 ];
      time_t t = time( NULL );
      strftime( buf, sizeof( buf ), "%Y-%m-%dT%H:%M:%SZ", gmtime( &t ) );
      return buf;
}
static void trim( char *s )
{
      char *e;
      while ( isspace( (unsigned char)*s ) )
            ++s;
      e = s + strlen( s );
      while ( e > s && isspace( (unsigned char)e[ -1 ] ) )
            --e;
      *e = '\0';
}

static const char *expand_home( const char *path )
{
      static char expanded[ PATH_MAX ];

      if ( strncmp( path, "$HOME", 5 ) == 0 )
      {
            safe_snprintf( expanded, sizeof( expanded ), "%s%s",
                           getenv( "HOME" ), path + 5 );
      }
      else if ( path[ 0 ] == '~' )
      {
            safe_snprintf( expanded, sizeof( expanded ), "%s%s",
                           getenv( "HOME" ), path + 1 );
      }
      else
      {
            strncpy( expanded, path, sizeof( expanded ) - 1 );
            expanded[ sizeof( expanded ) - 1 ] = '\0';
      }
      return expanded;
}

/* ------------------------------------------------------------------ */
/* Log helpers                                                         */

static void write_log( const char *action, const char *comment )
{
      if ( !*active_id )
            return;

      char path[ PATH_MAX ];
      safe_snprintf( path, sizeof( path ), "%s/%s.log", DATA_DIR, active_id );

      FILE *f = fopen( path, "a" );
      if ( !f )
      {
            fprintf( stderr, "Failed to open %s for writing\n", path );
            return;
      }

      /* ------  lock rules  ------------------------------------ */
      char locked[ 128 ] = "";
      lock_read( locked, sizeof locked );

      if ( strcmp( action, "checkin" ) == 0 )
      {
            if ( *locked && strcmp( locked, active_id ) != 0 )
            {
                  flash_msg( -1,
                             "Already checked-in to ‘%s’.  Check-out first.",
                             locked );
                  fclose( f );
                  return;
            }
            lock_write( active_id ); // Aquire lock
      }
      else if ( strcmp( action, "checkout" ) == 0 ||
                strcmp( action, "finish" ) == 0 ||
                strcmp( action, "cancel" ) == 0 )
      {

            lock_write( NULL ); /* clear the lock file  */
      }
      else if ( strcmp( action, "comment" ) == 0 )
      {
            if ( strcmp( locked, active_id ) != 0 )
            {
                  flash_msg( -1, "You must check-in before commenting." );
                  fclose( f );
                  return;
            }
      }

      fprintf( f, "%s\t%s\t%s\n", now_iso(), action, comment ? comment : "" );

      fclose( f );
}

static int compare_projects( const void *a, const void *b )
{
      const ProjectInfo *pa = a;
      const ProjectInfo *pb = b;
      return strcmp( pb->timestamp, pa->timestamp );
}

/* ------------------------------------------------------------------ */
/* State                                                      */

void load_active_project( void )
{
      char path[ PATH_MAX ];
      safe_snprintf( path, sizeof path, "%s/%s", STATE_DIR, RUNNING_FILE );

      FILE *f = fopen( path, "r" );
      if ( !f )
            return;

      if ( fgets( active_id, sizeof active_id, f ) )
            trim( active_id );
      fclose( f );

      /* verify the project still exists; otherwise forget it */
      char logpath[ PATH_MAX ];
      safe_snprintf( logpath, sizeof logpath, "%s/%s.log", DATA_DIR,
                     active_id );
      if ( access( logpath, F_OK ) != 0 ) /* file is gone */
            *active_id = '\0';
}

static void load_comments( void )
{
      comment_count = 0;

      if ( !*active_id )
            return;

      char path[ PATH_MAX ];
      safe_snprintf( path, sizeof( path ), "%s/%s.log", DATA_DIR, active_id );

      FILE *f = fopen( path, "r" );
      if ( !f )
            return;

      char line[ MAX_LINE ];
      while ( fgets( line, sizeof( line ), f ) )
      {
            if ( !strstr( line, "\tcomment\t" ) )
                  continue;

            char *ts = strtok( line, "\t" );
            strtok( NULL, "\t" ); // skip "comment"
            char *msg = strtok( NULL, "\n" );

            if ( ts && msg && comment_count < MAX_COMMENTS )
            {
                  strncpy( comments[ comment_count ].timestamp, ts,
                           sizeof( comments[ comment_count ].timestamp ) - 1 );
                  strncpy( comments[ comment_count ].message, msg,
                           sizeof( comments[ comment_count ].message ) - 1 );
                  ++comment_count;
            }
      }

      fclose( f );

      index_pos = comment_count ? comment_count - 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Data view                                                           */

static void draw_data_view( void )
{
      clear();
      draw_status_bar();
      //      mvprintw( 1, 0, "==== statlog (data view) ====" );

      if ( *active_id )
      {
            mvprintw( 2, 0, "Currently viewing: 🟢 %s", active_id );

            int total_min = compute_total_minutes();
            int h         = total_min / 60;
            int m         = total_min % 60;
            mvprintw( 4, 0, "🕒 Total time: %dh %02dm", h, m );

            // ── Show current project state with color
            char path[ PATH_MAX ];
            safe_snprintf( path, sizeof( path ), "%s/%s.log", DATA_DIR,
                           active_id );
            FILE *f                = fopen( path, "r" );
            char last_action[ 32 ] = "unknown";

            if ( f )
            {
                  char line[ MAX_LINE ];
                  while ( fgets( line, sizeof( line ), f ) )
                  {
                        char *ts  = strtok( line, "\t" );
                        char *act = strtok( NULL, "\t" );

                        if ( ts && act &&
                             ( strcmp( act, "checkin" ) == 0 ||
                               strcmp( act, "checkout" ) == 0 ||
                               strcmp( act, "created" ) == 0 ||
                               strcmp( act, "finish" ) == 0 ||
                               strcmp( act, "cancel" ) == 0 ) )
                        {
                              strncpy( last_action, act,
                                       sizeof( last_action ) - 1 );
                              last_action[ sizeof( last_action ) - 1 ] = '\0';
                        }
                  }
                  fclose( f );
            }

            int color = 6; // gray/fallback
            if ( strcmp( last_action, "checkin" ) == 0 )
                  color = 1;
            else if ( strcmp( last_action, "checkout" ) == 0 )
                  color = 2;
            else if ( strcmp( last_action, "created" ) == 0 )
                  color = 3;
            else if ( strcmp( last_action, "cancel" ) == 0 )
                  color = 4;
            else if ( strcmp( last_action, "finish" ) == 0 )
                  color = 5;
            else
                  color = 6; // default gray

            attron( COLOR_PAIR( color ) );
            mvprintw( 5, 0, "📍 Current state: %s", last_action );
            attroff( COLOR_PAIR( color ) );

            // ── Show current comment
            int base_line = 7;
            if ( comment_count && index_pos >= 0 && index_pos < comment_count )
            {
                  mvprintw( base_line, 0, "• %s",
                            comments[ index_pos ].message );
                  mvprintw( base_line + 1, 0, "• %d/%d  at %s", index_pos + 1,
                            comment_count, comments[ index_pos ].timestamp );
            }
            else
            {
                  mvprintw( base_line, 0, "• No comments yet" );
            }
      }
      else
      {
            mvprintw( 3, 0, "⚫  No active project" );
      }

      mvprintw( LINES - 2, 0,
                "[j/k] scroll  [c] comment  [d] delete  [l] list all  i/o/f/x "
                "= in/out/finish/cancel  [p] list  [q] quit" );

      refresh();
}

static void add_comment( void )
{
      echo();
      char msg[ MAX_LINE ];

      clear();
      mvprintw( 0, 0, "==== New comment for %s (empty = cancel) ====",
                *active_id ? active_id : "(none)" );
      mvprintw( 2, 0, "Comment: " );
      getnstr( msg, sizeof( msg ) - 1 );
      noecho();
      trim( msg );

      if ( *msg )
            write_log( "comment", msg );
      load_comments();
}

static void data_view_handle( int key )
{
      int need_reload = 0;

      switch ( key )
      {
      case 'j':
            if ( index_pos < comment_count - 1 )
                  ++index_pos;
            break;

      case 'k':
            if ( index_pos > 0 )
                  --index_pos;
            break;

      case 'c':
            if ( is_the_checked_in_project() )
            {
                  add_comment();
                  need_reload = 1;
            }
            else
            {
                  flash_msg( -1, "You must check-in before adding comments." );
            }
            break;

      case 'i':
            if ( has_checkin() )
            {
                  char locked[ 128 ] = "";
                  lock_read( locked, sizeof locked );
                  flash_msg( -1, "Already checked-in to ‘%s’.", locked );
            }
            else
            {
                  write_log( "checkin", NULL );
                  need_reload = 1;
            }
            break;

      case 'o':
            if ( is_the_checked_in_project() )
            {
                  write_log( "checkout", NULL );
                  need_reload = 1;
            }
            else
            {
                  flash_msg( -1, "Not checked-in to this project." );
            }
            break;

      case 'f':
            if ( is_the_checked_in_project() )
            {
                  write_log( "finish", NULL );
                  need_reload = 1;
            }
            else
            {
                  flash_msg( -1, "Not checked-in to this project." );
            }
            break;

      case 'x':
            if ( is_the_checked_in_project() )
            {
                  write_log( "cancel", NULL );
                  need_reload = 1;
            }
            else
            {
                  flash_msg( -1, "Not checked-in to this project." );
            }
            break;

      case 'd':
            if ( is_the_checked_in_project() )
            {
                  if ( *active_id && comment_count )
                  {
                        clear();
                        mvprintw( 0, 0, "Delete this comment? (y/n): " );
                        int ans = getch();
                        if ( ans == 'y' || ans == 'Y' )
                        {
                              delete_comment();
                              if ( index_pos > 0 )
                                    --index_pos; /* stay on previous entry */
                              load_comments();   // reload to reflect changes
                        }
                  }
            }
            else
            {
                  flash_msg( -1, "Not checked-in to this project." );
            }
            break;

      case 'l':
            if ( *active_id )
                  list_all_comments();
            break;

      case 'p':
      case KEY_BACKSPACE:
      case 127:
      case '\b':
            save_active_project( NULL ); /* clear running */
            mode = MODE_LIST;
            break;
      }

      if ( need_reload )
            load_comments();
}

/* ------------------------------------------------------------------ */
/* List view helpers                                                   */

static int is_status_match( const char *status, const char *filter )
{
      if ( strcmp( filter, "all" ) == 0 )
            return 1;
      if ( strcmp( filter, "created" ) == 0 )
            return strcmp( status, "created" ) == 0;
      return strcmp( status, filter ) == 0;
}

/* Build list, return total project count; also array of visible indexes */
/* ===== build_project_list: collect preview once ===== */
static int build_project_list( ProjectInfo *out, int *vis, int sel_filter,
                               int *vis_cnt )
{
      DIR *dp = opendir( DATA_DIR );
      if ( !dp )
            return 0;

      struct dirent *de;
      int cnt = 0;
      while ( ( de = readdir( dp ) ) )
      {
            if ( !strstr( de->d_name, ".log" ) )
                  continue;

            /* id ----------------------------------------- */
            char *dot  = strrchr( de->d_name, '.' );
            size_t len = dot - de->d_name;
            if ( len >= sizeof( out[ cnt ].id ) )
                  len = sizeof( out[ cnt ].id ) - 1;
            strncpy( out[ cnt ].id, de->d_name, len );
            out[ cnt ].id[ len ] = '\0';
            if ( !valid_id( out[ cnt ].id ) )
                  continue;

            /* default state */
            strcpy( out[ cnt ].last_action, "created" );
            strcpy( out[ cnt ].timestamp, "0000-00-00T00:00:00" );
            out[ cnt ].preview[ 0 ] = '\0';

            /* read log just once ------------------------- */
            char path[ PATH_MAX ];
            safe_snprintf( path, sizeof path, "%s/%s.log", DATA_DIR,
                           out[ cnt ].id );
            FILE *f = fopen( path, "r" );
            if ( f )
            {
                  char line[ MAX_LINE ];
                  while ( fgets( line, sizeof line, f ) )
                  {
                        char *ts  = strtok( line, "\t" );
                        char *act = strtok( NULL, "\t" );
                        if ( ts && act )
                        {
                              if ( strcmp( act, "comment" ) == 0 )
                              {
                                    char *msg = strtok( NULL, "\n" );
                                    if ( msg )
                                          strncpy(
                                              out[ cnt ].preview, msg,
                                              sizeof( out[ cnt ].preview ) -
                                                  1 );
                              }
                              else if ( !strcmp( act, "checkin" ) ||
                                        !strcmp( act, "checkout" ) ||
                                        !strcmp( act, "created" ) ||
                                        !strcmp( act, "finish" ) ||
                                        !strcmp( act, "cancel" ) )
                              {
                                    strncpy( out[ cnt ].last_action, act,
                                             sizeof( out[ cnt ].last_action ) -
                                                 1 );
                                    strncpy( out[ cnt ].timestamp, ts,
                                             sizeof( out[ cnt ].timestamp ) -
                                                 1 );
                              }
                        }
                  }
                  fclose( f );
            }
            ++cnt;
            if ( cnt >= MAX_PROJECTS )
                  break;
      }
      closedir( dp );

      qsort( out, cnt, sizeof( ProjectInfo ), compare_projects );

      const char *want = ( sel_filter == FLT_FINISH )    ? "finish"
                         : ( sel_filter == FLT_CANCEL )  ? "cancel"
                         : ( sel_filter == FLT_STARTED ) ? "created"
                                                         : "all";

      *vis_cnt = 0;
      for ( int i = 0; i < cnt; i++ )
            if ( is_status_match( out[ i ].last_action, want ) )
                  vis[ ( *vis_cnt )++ ] = i;
      return cnt;
}

/* ------------------------------------------------------------------ */
/* List view (interactive loop)                                        */

/* ===== light-weight list_view ===== */
/* --------------------------------------------------------------- */
/* Project list (interactive loop) – now with Archive / Delete     */
/* --------------------------------------------------------------- */
static void list_view( void )
{
      int filter = FLT_ALL, sel = 0;
      int need_reload = 1;

      ProjectInfo projects[ MAX_PROJECTS ];
      int vis[ MAX_PROJECTS ], vis_cnt = 0;

      save_active_project( NULL ); /* nothing “running” here   */

      for ( ;; )
      {
            if ( need_reload )
            {
                  build_project_list( projects, vis, filter, &vis_cnt );
                  if ( sel >= vis_cnt )
                        sel = vis_cnt ? vis_cnt - 1 : 0;
                  need_reload = 0;
            }

            /* ---------- paint ---------- */
            clear();
            draw_status_bar(); /* ★ */

            mvprintw(
                1, 0, /* ↓ */
                "==== Project list (↑/↓, ↵=open, "
                "a/f/c/s=filter, n=new, A=archive, x=delete, q=quit) ====" );

            mvprintw( 2, 0, "Filter: %s   (%d shown)", /* ↓ */
                      ( filter == FLT_FINISH )    ? "finished"
                      : ( filter == FLT_CANCEL )  ? "canceled"
                      : ( filter == FLT_STARTED ) ? "created"
                                                  : "all",
                      vis_cnt );

            int row = 4; /* ↓ */
            for ( int i = 0; i < vis_cnt; i++ )
            {
                  const ProjectInfo *p = &projects[ vis[ i ] ];
                  const char *sym      = "🗂";
                  int color            = 6;
                  if ( !strcmp( p->last_action, "checkin" ) )
                  {
                        sym   = "🟢";
                        color = 1;
                  }
                  else if ( !strcmp( p->last_action, "checkout" ) )
                  {
                        sym   = "🔴";
                        color = 2;
                  }
                  else if ( !strcmp( p->last_action, "created" ) )
                  {
                        sym   = "🟡";
                        color = 3;
                  }
                  else if ( !strcmp( p->last_action, "cancel" ) )
                  {
                        sym   = "✘";
                        color = 4;
                  }
                  else if ( !strcmp( p->last_action, "finish" ) )
                  {
                        sym   = "✔";
                        color = 5;
                  }

                  if ( i == sel )
                        attron( A_REVERSE );
                  attron( COLOR_PAIR( color ) );
                  mvprintw( row, 0, "%s %-12s", sym, p->id );
                  attroff( COLOR_PAIR( color ) );
                  mvprintw( row, 20, "(%s)", p->timestamp );
                  mvprintw( row, 42, "%.30s", p->preview );
                  if ( i == sel )
                        attroff( A_REVERSE );
                  ++row;
            }
            if ( !vis_cnt )
                  mvprintw( 5, 0, "(no projects match this filter)" ); /* ↓ */

            refresh();

            /* ---------- input ---------- */
            int ch = getch();
            switch ( ch )
            {
            /* global quit ------------------------------------------------ */
            case 'q':
                  mode = MODE_QUIT;
                  return;

            /* navigation ------------------------------------------------- */
            case KEY_UP:
            case 'k':
                  if ( sel > 0 )
                        --sel;
                  break;
            case KEY_DOWN:
            case 'j':
                  if ( sel < vis_cnt - 1 )
                        ++sel;
                  break;

            /* filters – need refresh ------------------------------------- */
            case 'a':
                  filter      = FLT_ALL;
                  need_reload = 1;
                  sel         = 0;
                  break;
            case 'f':
                  filter      = FLT_FINISH;
                  need_reload = 1;
                  sel         = 0;
                  break;
            case 'c':
                  filter      = FLT_CANCEL;
                  need_reload = 1;
                  sel         = 0;
                  break;
            case 's':
                  filter      = FLT_STARTED;
                  need_reload = 1;
                  sel         = 0;
                  break;

            /* make new project ------------------------------------------- */
            case 'n':
                  create_new_project();
                  need_reload = 1;
                  break;

                  /* ---------  NEW HOTKEYS ------------------------------------
                   */

                  /* A – archive (move to ARCHIVE_DIR) */
            /* A – archive (move to ARCHIVE_DIR) */
            case 'A':
                  if ( vis_cnt )
                  {
                        const ProjectInfo *p = &projects[ vis[ sel ] ];
                        clear();
                        mvprintw( 0, 0, "Archive project '%s'? [y/N] ", p->id );
                        int ans = getch();
                        if ( ans == 'y' || ans == 'Y' )
                        {
                              char src[ PATH_MAX ], dst[ PATH_MAX ];
                              safe_snprintf( src, sizeof src, "%s/%s.log",
                                             DATA_DIR, p->id );
                              safe_snprintf( dst, sizeof dst, "%s/%s.log",
                                             ARCHIVE_DIR, p->id );
                              rename( src, dst );

                              /* if archived project was checked-in → clear lock
                               */
                              char locked[ 128 ] = "";
                              lock_read( locked, sizeof locked );
                              if ( strcmp( locked, p->id ) == 0 )
                                    lock_write( NULL );

                              /* if archived project was open in UI → clear
                               * running */
                              if ( strcmp( active_id, p->id ) == 0 )
                                    save_active_project( NULL );

                              need_reload = 1;
                        }
                  }
                  break;

            /* x – delete permanently */
            case 'x':
                  if ( vis_cnt )
                  {
                        const ProjectInfo *p = &projects[ vis[ sel ] ];
                        clear();
                        mvprintw( 0, 0, "DELETE project '%s' forever? [y/N] ",
                                  p->id );
                        int ans = getch();
                        if ( ans == 'y' || ans == 'Y' )
                        {
                              char src[ PATH_MAX ];
                              safe_snprintf( src, sizeof src, "%s/%s.log",
                                             DATA_DIR, p->id );
                              unlink( src ); /* permanently delete */

                              /* if deleted project was checked-in → clear lock
                               */
                              char locked[ 128 ] = "";
                              lock_read( locked, sizeof locked );
                              if ( strcmp( locked, p->id ) == 0 )
                                    lock_write( NULL );

                              /* if deleted project was open in UI → clear
                               * running */
                              if ( strcmp( active_id, p->id ) == 0 )
                                    save_active_project( NULL );

                              need_reload = 1;
                        }
                  }
                  break;

            /* open project ----------------------------------------------- */
            case '\n':
            case KEY_ENTER:
                  if ( vis_cnt )
                  {
                        strncpy( active_id, projects[ vis[ sel ] ].id,
                                 sizeof active_id - 1 );
                        save_active_project( active_id );
                        load_comments();
                        mode = MODE_DATA;
                        return;
                  }
                  break;
            }
      }
}

/* ------------------------------------------------------------------ */
/* Project creation                                                    */

static void create_new_project( void )
{
      /* ── block while another project is checked-in ───────────────── */
      if ( has_checkin() )
      {
            char locked[ 128 ] = "";
            lock_read( locked, sizeof locked );
            flash_msg( -1,
                       "Already checked-in to ‘%s’.  Finish / check-out first.",
                       *locked ? locked : "(unknown)" );
            return;
      }

      echo();
      char id[ 128 ];

      clear();
      mvprintw( 0, 0, "==== Create New Project ====" );
      mvprintw( 2, 0, "ID (no spaces): " );
      getnstr( id, sizeof( id ) - 1 );
      noecho();

      trim( id );
      if ( !*id )
            return;

      if ( !valid_id( id ) )
      {
            mvprintw(
                4, 0,
                "Invalid ID (letters, digits, - or _ only).  Press any key…" );
            getch();
            return;
      }
      // Set as active
      strncpy( active_id, id, sizeof( active_id ) - 1 );
      active_id[ sizeof( active_id ) - 1 ] = '\0';

      save_active_project( id );

      write_log( "created", NULL );
      load_comments();
      mode = MODE_DATA;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */

int main( int argc, char *argv[] )
{
      setlocale( LC_ALL, "" ); /* enable UTF-8 symbols */

      if ( argc < 2 )
      {
            fprintf( stderr, "Usage: %s <base_dir>\n", argv[ 0 ] );
            return 1;
      }

      /* expand ~ or $HOME if present */
      const char *src = expand_home( argv[ 1 ] );

      safe_snprintf( BASE_DIR, sizeof BASE_DIR, "%s", src );
      safe_snprintf( DATA_DIR, sizeof DATA_DIR, "%s/data", BASE_DIR );
      safe_snprintf( STATE_DIR, sizeof STATE_DIR, "%s/state", BASE_DIR );
      safe_snprintf( ARCHIVE_DIR, sizeof ARCHIVE_DIR, "%s/archived", BASE_DIR );

      if ( *DATA_DIR )
            mkdir( DATA_DIR, 0755 );
      if ( *STATE_DIR )
            mkdir( STATE_DIR, 0755 );
      if ( *ARCHIVE_DIR )
            mkdir( ARCHIVE_DIR, 0755 );

      load_active_project();
      load_comments();

      /* decide the first screen ---------------------------------------- */
      if ( *active_id ) /* an active project was found */
            mode = MODE_DATA;
      else
            mode = MODE_LIST;

      initscr();
      keypad( stdscr, TRUE );
      raw();
      noecho();
      curs_set( 0 );

      int ch;

      start_color();
      use_default_colors();
      init_pair( 1, COLOR_MAGENTA, -1 ); // 🔄 checkin — magenta
      init_pair( 2, COLOR_YELLOW, -1 );  // ⛔ checkout — yellow
      init_pair( 3, COLOR_CYAN, -1 );    // 🟡 started — cyan
      init_pair( 4, COLOR_RED, -1 );     // ✘ cancel — red
      init_pair( 5, COLOR_GREEN, -1 );   // ✔ finish — green
      init_pair( 6, COLOR_WHITE, -1 );   // fallback/default

      if ( mode == MODE_LIST )
            list_view(); /* paint list immediately */

      while ( 1 )
      {
            if ( mode == MODE_QUIT || ch == 'q' )
            {
                  save_active_project( NULL );
                  break;
            }
            if ( mode == MODE_DATA )
                  draw_data_view();
            ch = getch();

            if ( ch == 'q' )
            {
                  if ( mode == MODE_LIST )
                  {
                        /* already in list view */
                        save_active_project( NULL ); /* forget running */
                        /* fall through to leave the program            */
                  }
                  break; /* quit program */
            }

            if ( mode == MODE_DATA )
            {
                  if ( ch == 'n' )
                  {
                        if ( has_checkin() )
                        {
                              char locked[ 128 ] = "";
                              lock_read( locked, sizeof locked );
                              flash_msg(
                                  -1,
                                  "Already checked-in to ‘%s’.  Finish / "
                                  "check-out first.",
                                  *locked ? locked : "(unknown)" );
                        }
                        else
                              create_new_project();
                  }
                  else if ( ch == 'p' )
                  {
                        mode = MODE_LIST; /* switch mode                 */
                        list_view();      /* open project list NOW       */
                        continue;         /* redraw on return            */
                  }
                  else
                  {
                        data_view_handle( ch );
                  }
            }
            else if ( mode == MODE_LIST )
            {
                  ungetch( ch ); /* push back to be read by list_view loop */
                  list_view();
            }
      }

      endwin();
      return 0;
}
