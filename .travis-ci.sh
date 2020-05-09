#!/bin/bash

if [ "$TASK" = "spellcheck" ]; then
	result=0
	# Create list of files to be spellchecked
	spellcheck_files=$(find . -type f | grep -v ".git/")

	# Run spellintian to find spelling errors
	sl_results=$(xargs spellintian 2>&1 <<< "$spellcheck_files")

	sl_errors=$(wc -l <<< "$sl_results")
	sl_errors_dups=$((grep "\(duplicate word\)" | wc -l) <<< "$sl_results")
	sl_errors_nodups=$((grep -v "\(duplicate word\)" | wc -l) <<< "$sl_results")

	if [ "$sl_errors" -ne 0 ]; then
		printf "Spellintian found %s errors (%s spelling, %s duplicate words):\n\n" "$sl_errors" "$sl_errors_nodups" "$sl_errors_dups"
		printf "%s\n\n" "$sl_results"
		result=1
	else
		printf "Spellintian reports no errors\n"
	fi

	# Run codespell to find some more
	cs_results=$(xargs codespell --quiet 2 <<< "$spellcheck_files" 2>&1)
	cs_errors=$(wc -l <<< "$cs_results")
	if [ "$cs_errors" -ne 0 ]; then
		printf "Codespell found %s errors:\n\n" "$cs_errors"
		printf "%s\n\n" "$cs_results"
		result=1
	else
		printf "Codespell reports no errors\n"
	fi
	exit "$result"
elif [ "$TASK" = "codesmell" ]; then
	result=0

	if [ -z "$(which lizard)" ]; then
		printf "Installing lizard...\n"
		pip3 install lizard
	fi

	# Run shellcheck for all shell scripts
	printf "Running shellcheck...\n"
	shell_files="$(find . -type f -iname \*.sh)"
	xargs shellcheck -Cnever -s bash <<< "$shell_files"
	if [ "$?" -ne "0" ]; then
		result=1
	fi

	# Run cloc for some stats
	printf "Code statistics:\n\n"
	cloc ./

	# Run lizard for the project
	printf "Running lizard for code complexity analysis\n"
	lizard ./
	if [ "$?" -ne "0" ]; then
		result=1
	fi

	exit "$result"
elif [ "$TASK" = "sanitize" ]; then
	# Run sanitized compile
	travis_fold start "make_sanitize"
	if ! make sanitize; then
		exit "$?"
	fi
	travis_fold end "make_sanitize"
elif [ "$TASK" = "windows" ]; then
	travis_fold start "make_windows"
	if ! make windows; then
		exit "$?"
	fi
	make -C backends lua.dll
	travis_fold end "make_windows"
	if [ "$(git describe)" == "$(git describe --abbrev=0)" ] || [ -n "$DEPLOY" ]; then
		travis_fold start "deploy_windows"
		mkdir ./deployment
		mkdir ./deployment/backends
		mkdir ./deployment/docs
		# Strip the Windows binaries as they become huge quickly
		strip midimonster.exe backends/*.dll
		cp ./midimonster.exe ./deployment/
		cp ./backends/*.dll ./deployment/backends/
		cp ./backends/*.dll.disabled ./deployment/backends/
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
	if ! make full; then
		exit "$?"
	fi
	travis_fold end "make"
	if [ "$(git describe)" == "$(git describe --abbrev=0)" ] || [ -n "$DEPLOY" ]; then
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
		tar czf "midimonster-$(git describe)-$TRAVIS_OS_NAME.tgz" ./
		find . ! -iname '*.tgz' -delete
		travis_fold end "deploy_unix"
	fi
fi
