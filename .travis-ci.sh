#!/bin/bash

# This script is triggered from the script section of .travis.yml
# It runs the appropriate commands depending on the task requested.

set -e

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
elif [[ $TASK = 'sanitize' ]]; then
	# Run sanitized compile
	travis_fold start "make_sanitize"
	make sanitize;
	travis_fold end "make_sanitize"
elif [[ $TASK = 'windows' ]]; then
	travis_fold start "make_windows"
	make windows;
	make -C backends lua.dll
	travis_fold end "make_windows"
	if [ "$(git describe)" == "$(git describe --abbrev=0)" ]; then
		travis_fold start "deploy_windows"
		mkdir ./deployment
		mkdir ./deployment/backends
		mkdir ./deployment/docs
		cp ./midimonster.exe ./deployment/
		cp ./backends/*.dll ./deployment/backends/
		cp ./monster.cfg ./deployment/monster.cfg
		cp ./backends/*.md ./deployment/docs/
		cp -r ./configs ./deployment/
		cd ./deployment
		zip -r "./midimonster-$(git describe)-windows.zip" "./"
		find . ! -iname '*.zip' -delete
		travis_fold end "deploy_windows"
	fi
else
	# Otherwise compile as normal
	travis_fold start "make"
	make full;
	travis_fold end "make"
	if [ "$(git describe)" == "$(git describe --abbrev=0)" ]; then
		travis_fold start "deploy_unix"
		mkdir ./deployment
		mkdir ./deployment/backends
		mkdir ./deployment/docs
		cp ./midimonster ./deployment/
		cp ./backends/*.so ./deployment/backends/
		cp ./monster.cfg ./deployment/monster.cfg
		cp ./backends/*.md ./deployment/docs/
		cp -r ./configs ./deployment/
		cd ./deployment
		tar czf "midimonster-$(git describe)-$TRAVIS_OS_NAME.tgz" *
		find . ! -iname '*.tgz' -delete
		travis_fold end "deploy_unix"
	fi
fi
