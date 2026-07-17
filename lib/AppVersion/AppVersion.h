#pragma once

// PlatformIO normally supplies this via scripts/git_branch.py. Keep a fallback here so editor
// indexers and static-analysis tools still parse cleanly without running the full build.
#ifndef RUBY_VERSION
#define RUBY_VERSION "dev"
#endif
