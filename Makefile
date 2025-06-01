# Directory variables
SRC_DIR     := src
INSTALL_DIR := /usr/local/bin
BIN_NAME    := adash
SRC_FILE    := $(SRC_DIR)/$(BIN_NAME).c

CFLAGS      := -Wall -Wextra -O2
LDFLAGS     := -lncurses

# Default target: build the binary
all: $(BIN_NAME)

$(BIN_NAME): $(SRC_FILE)
	@echo "Compiling $(SRC_FILE) → $(BIN_NAME)..."
	@$(CC) $(CFLAGS) $(SRC_FILE) -o $(BIN_NAME) $(LDFLAGS)

# Install target
install: $(BIN_NAME)
	@echo "Installing $(BIN_NAME) to $(INSTALL_DIR)..."
	@install -m 755 $(BIN_NAME) $(INSTALL_DIR)/$(BIN_NAME)

# Uninstall target
uninstall:
	@echo "Removing $(INSTALL_DIR)/$(BIN_NAME)..."
	@rm -f $(INSTALL_DIR)/$(BIN_NAME)

# Clean target
clean:
	@echo "Removing built binary..."
	@rm -f $(BIN_NAME)

# Phony targets
.PHONY: all install uninstall clean

