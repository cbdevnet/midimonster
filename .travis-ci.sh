#!/bin/bash

# This script is triggered from the script section of .travis.yml
# It runs the appropriate commands depending on the task requested.

set -e

COVERITY_SCAN_BUILD_URL="https://scan.coverity.com/scripts/travisci_build_coverity_scan.sh"

SPELLINGBLACKLIST=$(cat <<-BLACKLIST
      -wholename "./.git/*"
BLACKLIST
)

if [[ $TASK = 'spellintian' ]]; then
  # run spellintian only if it is the requested task, ignoring duplicate words
  spellingfiles=$(eval "find ./ -type f -and ! \( \
      $SPELLINGBLACKLIST \
      \) | xargs")
  # count the number of spellintian errors, ignoring duplicate words
  spellingerrors=$(zrun spellintian $spellingfiles 2>&1 | grep -v "\(duplicate word\)" | wc -l)
  if [[ $spellingerrors -ne 0 ]]; then
    # print the output for info
    zrun spellintian $spellingfiles | grep -v "\(duplicate word\)"
    echo "Found $spellingerrors spelling errors via spellintian, ignoring duplicates"
    exit 1;
  else
    echo "Found $spellingerrors spelling errors via spellintian, ignoring duplicates"
  fi;
elif [[ $TASK = 'spellintian-duplicates' ]]; then
  # run spellintian only if it is the requested task
  spellingfiles=$(eval "find ./ -type f -and ! \( \
      $SPELLINGBLACKLIST \
      \) | xargs")
  # count the number of spellintian errors
  spellingerrors=$(zrun spellintian $spellingfiles 2>&1 | wc -l)
  if [[ $spellingerrors -ne 0 ]]; then
    # print the output for info
    zrun spellintian $spellingfiles
    echo "Found $spellingerrors spelling errors via spellintian"
    exit 1;
  else
    echo "Found $spellingerrors spelling errors via spellintian"
  fi;
elif [[ $TASK = 'codespell' ]]; then
  # run codespell only if it is the requested task
  spellingfiles=$(eval "find ./ -type f -and ! \( \
      $SPELLINGBLACKLIST \
      \) | xargs")
  # count the number of codespell errors
  spellingerrors=$(zrun codespell --check-filenames --check-hidden --quiet 2 --regex "[a-zA-Z0-9][\\-'a-zA-Z0-9]+[a-zA-Z0-9]" $spellingfiles 2>&1 | wc -l)
  if [[ $spellingerrors -ne 0 ]]; then
    # print the output for info
    zrun codespell --check-filenames --check-hidden --quiet 2 --regex "[a-zA-Z0-9][\\-'a-zA-Z0-9]+[a-zA-Z0-9]" $spellingfiles
    echo "Found $spellingerrors spelling errors via codespell"
    exit 1;
  else
    echo "Found $spellingerrors spelling errors via codespell"
  fi;
elif [[ $TASK = 'coverity' ]]; then
  # Run Coverity Scan unless token is zero length
  # The Coverity Scan script also relies on a number of other COVERITY_SCAN_
  # variables set in .travis.yml
  if [[ ${#COVERITY_SCAN_TOKEN} -ne 0 ]]; then
    curl -s $COVERITY_SCAN_BUILD_URL | bash
  else
    echo "Skipping Coverity Scan as no token found, probably a Pull Request"
  fi;
elif [[ $TASK = 'sanitize' ]]; then
  # Run sanitized compile
  travis_fold start "make_sanitize"
  make sanitize;
  travis_fold end "make_sanitize"
else
  # Otherwise compile as normal
  travis_fold start "make"
  make;
  travis_fold end "make"
fi
