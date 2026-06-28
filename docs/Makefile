# ============================================================================
# Makefile — GhostBlade Documentation
#
# Copyright (C) 2026 GhostBlade Project
# SPDX-License-Identifier: CC-BY-SA-4.0
#
# Targets:
#   check    — Verify all internal markdown links are valid
#   links    — Same as 'check'
#   clean    — Remove generated files
#   help     — Show available targets
# ============================================================================

MARKDOWN_FILES := $(wildcard *.md) $(wildcard ../*.md)
PYTHON         ?= python3

.PHONY: check links clean help

check: links

links:
	@echo "Checking internal markdown links..."
	@$(PYTHON) ../tools/check_internal_links.py

clean:
	@echo "No generated documentation files to clean."

help:
	@echo "GhostBlade Documentation Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  check  — Verify all internal markdown links are valid"
	@echo "  links  — Same as 'check'"
	@echo "  clean  — Remove generated files (none currently)"
	@echo "  help   — Show this message"