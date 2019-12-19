#!/bin/bash

################################################ SETUP ################################################
deps=(libasound2-dev libevdev-dev liblua5.3-dev libjack-jackd2-dev pkg-config libssl-dev gcc make wget git)
user=$(whoami)                  # for bypassing user check replace "$(whoami)" with "root".

tmp_path=$(mktemp -d)           # Repo download path
updater_dir=/etc/midimonster-updater       # Updater download + config path
updater_file=$updater_dir/updater.conf

latest_version=$(curl --silent "https://api.github.com/repos/cbdevnet/midimonster/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/')

makeargs=all                    # Build args

VAR_DESTDIR=""                  # Unused
VAR_PREFIX="/usr"
VAR_PLUGINS="$VAR_PREFIX/lib/midimonster"
VAR_DEFAULT_CFG="/etc/midimonster/midimonster.cfg"
VAR_EXAMPLE_CFGS="$VAR_PREFIX/share/midimonster"

bold=$(tput bold)
normal=$(tput sgr0)

################################################ SETUP ################################################

############################################## FUNCTIONS ##############################################

INSTALL-DEPS () {           ##Install deps from array "$deps"
	for t in ${deps[@]}; do
	    if [ $(dpkg-query -W -f='${Status}' $t 2>/dev/null | grep -c "ok installed") -eq 0 ]; then
	        printf "Installing %s\n" "$t"
	        apt-get install $t;
	        printf "Done\n";
	    else
	        printf "%s already installed!\n" "$t"
	    fi
	done
	printf "\n"
}

NIGHTLY_CHECK () {
	#Asks for nightly version
	read -p "Do you want to install the latest development version? (y/n)? " magic
	case "$magic" in
		y|Y)
			printf "OK! You´re a risky person ;D\n"
			NIGHTLY=1
			;;
		n|N)
			printf "That´s OK - installing the latest stable version for you ;-)\n"
			NIGHTLY=0
			;;
		*)
			printf "Invalid input\n"
			ERROR
			;;
	esac

	# Roll back to last tag if we're not on a nightly build
	if [ "$NIGHTLY" != 1 ]; then
		printf "Finding latest stable version...\n"
		Iversion=$(git describe --abbrev=0)
		printf "Starting Git checkout to %s...\n" "$Iversion"
		git checkout -f -q $Iversion
	fi
}

INSTALL-PREP () {
	(
		printf "Starting download...\n"
		git clone https://github.com/cbdevnet/midimonster.git "$tmp_path" # Gets Midimonster
		printf "\nInitializing repository...\n"
		cd $tmp_path
		git init $tmp_path
		printf "\n"
	)
	NIGHTLY_CHECK
	printf "Preparation successful\n\n"
	printf "${bold}If you don't know what you're doing, just hit enter 4 times.${normal}\n"

	read -e -i "$VAR_PREFIX" -p "PREFIX (Install root directory): " input # Reads VAR_PREFIX
	VAR_PREFIX="${input:-$VAR_PREFIX}"

	read -e -i "$VAR_PLUGINS" -p "PLUGINS (Plugin directory): " input # Reads VAR_PLUGINS
	VAR_PLUGINS="${input:-$VAR_PLUGINS}"

	read -e -i "$VAR_DEFAULT_CFG" -p "Default config path: " input # Reads VAR_DEFAULT_CFG
	VAR_DEFAULT_CFG="${input:-$VAR_DEFAULT_CFG}"

	read -e -i "$VAR_EXAMPLE_CFGS" -p "Example config directory: " input # Reads VAR_EXAMPLE_CFGS
	VAR_EXAMPLE_CFGS="${input:-$VAR_EXAMPLE_CFGS}"

	UPDATER_SAVE

	export PREFIX=$VAR_PREFIX
	export PLUGINS=$VAR_PLUGINS
	export DEFAULT_CFG=$VAR_DEFAULT_CFG
	export DESTDIR=$VAR_DESTDIR
	export EXAMPLES=$VAR_EXAMPLE_CFGS
}

UPDATER-PREP () {
	(
		printf "Starting download...\n"
		git clone https://github.com/cbdevnet/midimonster.git "$tmp_path" # Gets Midimonster
		printf "\nInitializing repository...\n"
		cd $tmp_path
		git init $tmp_path
		printf "Sucessfully imported settings from %s\n" "$updater_file"
	)
	NIGHTLY_CHECK
	printf "Preparation successful\n\n"

	rm -f "$VAR_PREFIX/bin/midimonster"
	rm -rf "$VAR_PLUGINS/"

	UPDATER_SAVE

	export PREFIX=$VAR_PREFIX
	export PLUGINS=$VAR_PLUGINS
	export DEFAULT_CFG=$VAR_DEFAULT_CFG
	export DESTDIR=$VAR_DESTDIR
	export EXAMPLES=$VAR_EXAMPLE_CFGS
}

UPDATER () {
	installed_version="$(midimonster --version)"
	#installed_version="MIDIMonster v0.3-40-gafed325" # FOR TESTING ONLY! (or bypassing updater version check)
	if [[ "$installed_version" =~ "$latest_version" ]]; then
		printf "Newest Version is already installed! ${bold}($installed_version)${normal}\n\n"
		ERROR
	else
		printf "The installed Version ${bold}´$installed_version´${normal} equals not the newest stable version ${bold}´$latest_version´${normal} (Maybe you are running a development version?)\n\n"
	fi

	UPDATER-PREP
	INSTALL-RUN
	DONE
}

INSTALL-RUN () {                                    # Build
	cd "$tmp_path"
	make clean
	make $makeargs
	make install
}

UPDATER_SAVE () {                                   # Saves file for the auto updater in this script
	rm -rf $updater_dir
	printf "Saving updater to %s/updater.sh\n" "$update_dir"
	mkdir -p "$updater_dir"
	wget https://raw.githubusercontent.com/cbdevnet/midimonster/master/installer.sh -O $updater_dir/updater.sh
	printf "Creating symlink to updater in /usr/bin/midimonster-updater\n"
	ln -s "$updater_dir/updater.sh" "/usr/bin/midimonster-updater"
	chmod +x "$updater_dir/updater.sh"
	printf "Exporting updater config to %s\n" "$updater_file"
	printf "VAR_PREFIX=%s\nVAR_PLUGINS=%s\nVAR_DEFAULT_CFG=%s\nVAR_DESTDIR=%s\nVAR_EXAMPLE_CFGS=%s\n" "$VAR_PREFIX" "$VAR_PLUGINS" "$VAR_DEFAULT_CFG" "$VAR_DESTDIR" "$VAR_EXAMPLE_CFGS" > $updater_file
}

ERROR () {
	printf "\nAborting...\n"
	CLEAN
	printf "Exiting...\n"
	exit 1
}

DONE () {
	printf "\nDone.\n"
	CLEAN
	exit 0
}

CLEAN () {
	printf "\nCleaning...\n"
	rm -rf $tmp_path
}

############################################## FUNCTIONS ##############################################


################################################ Main #################################################
trap ERROR SIGINT SIGTERM SIGKILL
clear

# Check if $user = root!
if [ "$user" != "root" ]; then
	printf "Installer must be run as root\n"
	ERROR
fi

if [ $(wget -q --spider http://github.com) $? -eq 1 ]; then
	printf "You need connection to the internet\n"
	ERROR
fi

# Check if updater config file exist and import it (overwrites default values!)
if [ -f $updater_file ]; then
	printf "Starting updater...\n\n"
	. $updater_file

	# Check if binary $updater/bin/midimonster exist. If yes start updater else skip.
	if [ -x "$VAR_PREFIX/bin/midimonster" ]; then
		UPDATER
	else
		printf "midimonster binary not found, skipping updater.\n"
	fi
fi

INSTALL-DEPS
INSTALL-PREP
printf "\n"
INSTALL-RUN
DONE
