#!/bin/bash
# shellcheck disable=SC2001,SC2181,SC1117

################################################ SETUP ################################################
dep_build_core=(
	libasound2-dev
	libevdev-dev
	liblua5.3-dev
	libola-dev
	libjack-jackd2-dev
	python3-dev
	libssl-dev
	build-essential
	pkg-config
	git
)

dep_build_osx=(
	ola
	lua
	openssl@1.1
	jack
	python3
)

dep_build_win=(
	mingw-w64
)

dep_build_debian=(
	git-buildpackage
	debhelper
)

exitcode="0"

############################################## FUNCTIONS ##############################################

ARGS(){
	for i in "$@"; do
		case "$i" in
			--target=*|-t=*)
				TARGETS="${i#*=}"
			;;
			--deploy)
				deploy="1"
			;;
			--deps)
				install_deps="1"
			;;
			-v|--verbose)
				verbose="1"
			;;
			-af|--allow-failure)
				allow_failure="1"
			;;
			-h|--help|*)
				print_help
				exit "0"
			;;
		esac
		shift
	done
	[[ -z $TARGETS ]] && print_help && printf "\nNo target specified!\n" && exit "1"	# If no target(s) are specified exit.
}

print_help() {
	printf "Usage: %s [OPTIONS]\n\n" "$0"
	printf -- "-t=<argument>, <argument>\t--target=<argument>, <argument>\n\n"
	printf -- "--deploy\tPack releases to the ./deployment/\$target directory.\n"
	printf -- "--deps\t\tCheck and install all dependencies needed for the specified target prior to target run.\n"
	printf -- "-af, --allow-failure\tAlways exit with code 0.\n"
	printf -- "-v, --verbose\tEnables detailed log output.\n\n"
	printf "Valid test targets are: \t\"check-spelling\" - \"1\", \"check-codespelling\" - \"2\", \"analyze-complexity\" - \"3\", \"analyze-shellscript\" - \"4\", \"stats\" - \"5\".\n"
	printf "Valid build targets are: \t\"build-linux\" - \"10\", \"build-windows\" - \"11\", \"build-debian\" - \"12\".\n"
	printf "Valid dependency install targets are: \t\"deps-linux\", \"deps-windows\", \"deps-debian\", \"deps-osx\" \"deps-tests\", \"deps-all\".\n\n"
}

deps_apt(){
	start_apt update -y -qq > /dev/null || error_handler "There was an error doing apt update."
	for dependency in "$@"; do
		if [ "$(dpkg-query -W -f='${Status}' "$dependency" 2>/dev/null | grep -c "ok installed")" -eq 0 ]; then
			deps+=("$dependency")   # Add not installed dependency to the "to be installed array".
		else
			[[ -n $verbose ]] && printf "%s already installed!\n" "$dependency"   # If the dependency is already installed print it.
		fi
	done

	if [ ! "${#deps[@]}" -ge "1" ]; then	# If nothing needs to get installed don't start apt.
		[[ -n $verbose ]] && echo "All dependencies are fulfilled."	# Dependency array empty! Not running apt!
	else
		[[ -z $verbose ]] && echo "Starting dependency installation."
		[[ -n $verbose ]] && echo "Then following dependencies are going to be installed:"	# Dependency array contains items. Running apt.
		[[ -n $verbose ]] && echo "${deps[@]}" | sed 's/ /, /g'
		start_apt install -y -qq --no-install-suggests --no-install-recommends "${deps[@]}" > /dev/null || error_handler "There was an error doing dependency installation!"
	fi
	[[ -n $verbose ]] && printf "\n"
}

start_apt(){
	i="0"
	if command -v fuser &> /dev/null; then
		while fuser /var/lib/dpkg/lock >/dev/null 2>&1 ; do
			[ "$i" -eq "0" ] && printf "\nWaiting for other software managers to finish"
			[ "$i" -le "16" ] && printf "." # Print a max of 16 dots if waiting.
			((i=i+1))
			sleep "1s"
		done
		[ "$i" -ge "1" ] && printf "ready!\n"
	fi
	DEBIAN_FRONTEND=noninteractive apt-get "$@"
}

deps_brew(){
	# 'brew install' sometimes returns non-zero for some arcane reason. 
	for dependency in "$@"; do
		brew install "$dependency"
	done
	brew link --overwrite python
}

# Build targets and corresponding deployment.

build-linux(){
	[[ -n $install_deps ]] && deps_apt "${dep_build_core[@]}"
	make full
}

build-osx(){
	# OpenSSL is not a proper install due to some Apple bull, so provide additional locations via the environment...
	# Additionally, newer versions of this "recipe" seem to use the name 'openssl@1.1' instead of plain 'openssl' and there seems to be
	# no way to programmatically get the link and include paths. Genius! Hardcoding the new version for the time being...
	export CFLAGS="$CFLAGS -I/usr/local/opt/openssl@1.1/include"
	export LDFLAGS="$LDFLAGS -L/usr/local/opt/openssl@1.1/lib"
	make full
}

build-windows(){
	[[ -n $install_deps ]] && deps_apt "${dep_build_core[@]}" "${dep_build_win[@]}"
	# Download libraries to link with for Windows
	wget "https://downloads.sourceforge.net/project/luabinaries/5.3.5/Windows%20Libraries/Dynamic/lua-5.3.5_Win64_dllw6_lib.zip" -O lua53.zip
	unzip lua53.zip lua53.dll
	make windows
	make -C backends lua.dll
}

build-debian(){
	[[ -n $install_deps ]] && deps_apt "${dep_build_core[@]}" "${dep_build_debian[@]}"
	git checkout debian/master
	gbp buildpackage
}

build-linux-deploy(){
	#printf "\nLinux Deployment started..\n"
	mkdir -p ./deployment/linux/backends
	mkdir -p ./deployment/linux/docs
	cp ./midimonster ./deployment/linux/
	cp ./backends/*.so ./deployment/linux/backends/
	cp ./monster.cfg ./deployment/linux/monster.cfg
	cp ./backends/*.md ./deployment/linux/docs/
	cp -r ./configs ./deployment/linux/
	cd ./deployment/linux || error_handler "Error doing cd to ./deployment"
	filename="midimonster-$(git describe)-$OS.tgz"
	touch "$filename" && tar --exclude=*.tgz -czf "$filename" "./"
	find . ! -iname "*.zip" ! -iname "*.tgz" -delete
}

build-windows-deploy(){
	#printf "\nWindows Deployment started..\n"
	mkdir -p ./deployment/windows/backends
	mkdir -p ./deployment/windows/docs
	strip midimonster.exe backends/*.dll	# Strip the Windows binaries as they become huge quickly.
	cp ./midimonster.exe ./deployment/windows/
	cp ./backends/*.dll ./deployment/windows/backends/
	cp ./backends/*.dll.disabled ./deployment/windows/backends/
	cp ./monster.cfg ./deployment/windows/monster.cfg
	cp ./backends/*.md ./deployment/windows/docs/
	cp -r ./configs ./deployment/windows/
	cd ./deployment/windows || error_handler "Error doing cd to ./deployment/windows"
	zip -r "./midimonster-$(git describe)-windows.zip" "./"
	find . ! -iname "*.zip" ! -iname "*.tgz" -delete
}

build-debian-deploy(){
	#printf "\nDebian Package Deployment started..\n"
	mkdir -p ./deployment/debian/
	cp ./*.deb ./deployment/debian/
}

# Tests

ckeck-spelling(){		# Check spelling.
	[[ -n $install_deps ]] && deps_apt "lintian"
	spellcheck_files=$(find . -type f | grep -v ".git/")	# Create list of files to be spellchecked.
	sl_results=$(xargs spellintian 2>&1 <<< "$spellcheck_files")	# Run spellintian to find spelling errors
	sl_errors=$(wc -l <<< "$sl_results")
	sl_errors_dups=$( (grep -c "\(duplicate word\)") <<< "$sl_results")
	sl_errors_nodups=$( (grep -cv "\(duplicate word\)") <<< "$sl_results")

	if [ "$sl_errors" -gt "1" ]; then
		printf "Spellintian found %s errors (%s spelling, %s duplicate words):\n\n" "$sl_errors" "$sl_errors_nodups" "$sl_errors_dups"
		printf "%s\n\n" "$sl_results"
		exitcode=1
	else
		printf "Spellintian reports no errors\n"
	fi
}

check-codespelling(){	# Check code for common misspellings.
	[[ -n $install_deps ]] && deps_apt "codespell"
	spellcheck_files=$(find . -type f | grep -v ".git/")	# Create list of files to be spellchecked.
	cs_results=$(xargs codespell --quiet 2 <<< "$spellcheck_files" 2>&1)
	cs_errors=$(wc -l <<< "$cs_results")
	if [ "$cs_errors" -gt "1" ]; then
		printf "Codespell found %s errors:\n\n" "$cs_errors"
		printf "%s\n\n" "$cs_results"
		exitcode=1
	else
		printf "Codespell reports no errors\n"
	fi
}

analyze-complexity(){	# code complexity analyser.
	[[ -n $install_deps ]] && deps_apt "python3" "python3-pip"
	if [ -z "$(which ~/.local/bin/lizard)" ]; then
		printf "Installing lizard...\n"
		pip3 install lizard >/dev/null
	fi
	printf "Running lizard for code complexity analysis\n"
	~/.local/bin/lizard ./
	if [ "$?" -ne "0" ]; then
		exitcode=1
	fi
}

analyze-shellscript(){	# Shellscript analysis tool.
	[[ -n $install_deps ]] && deps_apt "shellcheck"
	printf "Running shellcheck:\n"
	shell_files="$(find . -type f -iname \*.sh)"
	xargs shellcheck -Cnever -s bash <<< "$shell_files"
	if [ "$?" -ne "0" ]; then
		exitcode=1
	fi
}

stats(){				# Code statistics.
	[[ -n $install_deps ]] && deps_apt "cloc"
	printf "Code statistics:\n"
	cloc ./
}

target_queue(){
	printf "\n"
	IFS=',|.' read -ra Queue <<< "$TARGETS"
	for i in "${Queue[@]}"; do
		case "$i" in
			check-spelling|1)
				ckeck-spelling
			;;
			check-codespelling|2)
				check-codespelling
			;;
			analyze-complexity|3)
				analyze-complexity
			;;
			analyze-shellscript|4)
				analyze-shellscript
			;;
			stats|5)
				stats
			;;
			build-linux|10)
				OS="linux"
				build-linux
				[[ -n $deploy ]] && build-linux-deploy
			;;
			build-windows|build-win|11)
				build-windows
				[[ -n $deploy ]] && build-windows-deploy
			;;
			build-debian|build-deb|12)
				build-debian
				[[ -n $deploy ]] && build-debian-deploy
			;;
			build-osx|13)
				OS="osx"
				build-osx
				[[ -n $deploy ]] && build-linux-deploy
			;;
			deps-linux)
				# Target to install all needed dependencies for linux builds.
				deps_apt "${dep_build_core[@]}"
			;;
			deps-windows|deps-win)
				# Target to install all needed dependencies for windows builds.
				deps_apt "${dep_build_core[@]}" "${dep_build_win[@]}"
			;;
			deps-debian|deps-deb)
				# Target to install all needed dependencies for debian packaging.
				deps_apt "${dep_build_core[@]}" "${dep_build_debian[@]}"
			;;
			deps-osx)
				# Target to install all needed dependencies for osx.
				printf "\nNot implemented yet!\n"
				deps_brew "${dep_build_osx[@]}"
			;;
			deps-tests)
				deps_apt "lintian" "codespell" "python3" "python3-pip" "shellcheck" "cloc"
				# Install lizard if not found.
				if [ -z "$(which ~/.local/bin/lizard)" ]; then
					pip3 install lizard >/dev/null
				fi
			;;
			deps-all)
				# Target to install all needed dependencies for this ci script.
				deps_apt "${dep_build_core[@]}" "${dep_build_win[@]}" "${dep_build_debian[@]}" "lintian" "codespell" "python3" "python3-pip" "shellcheck" "cloc"
			;;
			*)
				printf "Target '%s' not valid!\n" "$i"
			;;
		esac
		printf "\n"
	done
}

error_handler(){
	[[ -n $1 ]] && printf "\n%s\n" "$1"
	printf "\nAborting"
	for i in {1..3}; do sleep 0.3s && printf "." && sleep 0.2s; done
	printf "\n"
	exit "1"
}

################################################ Main #################################################
trap error_handler SIGINT SIGTERM

ARGS "$@"	# Parse arguments.
target_queue	# Start requestet targets.

# Allow failure handler.
[[ -z $allow_failure ]] && exit "$exitcode"
exit "0"
