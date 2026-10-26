#!/bin/sh
# ci/version.sh — print the version a build should call itself.
#
# One implementation, because three places need the same answer: the bundle
# name, the Debian package version and the release notes. A tag is the version
# when there is one, in either CI and regardless of which CI it is; otherwise
# the git description, and "dev" when even that is unavailable (an out-of-tree
# build with no git, which buildroot does).

set -eu

if [ -n "${VNOVEL_VERSION:-}" ]; then
	printf '%s\n' "$VNOVEL_VERSION"
elif [ -n "${CI_COMMIT_TAG:-}" ]; then
	# GitLab: set only for tag pipelines.
	printf '%s\n' "$CI_COMMIT_TAG"
elif [ -n "${GITHUB_REF_NAME:-}" ] && [ "${GITHUB_REF_TYPE:-}" = "tag" ]; then
	# GitHub Actions: both variables are set on every run, so the type must be
	# checked or a branch build would version itself after the branch name.
	printf '%s\n' "$GITHUB_REF_NAME"
else
	# --always: a shallow clone has no reachable tag, and this must still
	# print the commit rather than fail. --dirty: a local build is marked as
	# one, matching the Makefile's BUILD_VERSION stamp. ci/deb.sh strips the
	# marker, because Debian will not accept it in a version.
	git describe --tags --always --dirty 2>/dev/null || echo dev
fi
