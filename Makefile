# Directory variables
SRC_DIR     := src
INSTALL_DIR := /usr/local/bin
BIN_NAME    := adash
SRC_FILE    := $(SRC_DIR)/$(BIN_NAME)  # now the bash script

# No compilation flags needed for bash script

# Default target: "install" the script by copying it and setting executable permission
all: install

install: $(SRC_FILE)
	@echo "Installing $(BIN_NAME) to $(INSTALL_DIR)..."
	@install -m 755 $(SRC_FILE) $(INSTALL_DIR)/$(BIN_NAME)

uninstall:
	@echo "Removing $(INSTALL_DIR)/$(BIN_NAME)..."
	@rm -f $(INSTALL_DIR)/$(BIN_NAME)

clean:
	@echo "Nothing to clean for bash script."

.PHONY: all install uninstall clean

