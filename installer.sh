#!/bin/bash

################################################ SETUP ################################################
deps=(
	libasound2-dev
	libevdev-dev
	liblua5.3-dev
	libjack-jackd2-dev
	pkg-config
	libssl-dev
	python3-dev
	gcc
	make
	wget
	git
)
# Replace this with 'root' to bypass the user check
user="$(whoami)"
# Temporary directory used for repository clone
tmp_path="$(mktemp -d)"
# Installer/updater install directory
updater_dir="/etc/midimonster-updater"

latest_version="$(curl --silent "https://api.github.com/repos/cbdevnet/midimonster/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/')"

# make invocation arguments
makeargs="all"

normal="$(tput sgr0)"
dim="$(tput dim)"
bold="$(tput bold)"
uline="$(tput smul)"
c_red="$(tput setaf 1)"
c_green="$(tput setaf 2)"
c_mag="$(tput setaf 5)"

DEFAULT_PREFIX="/usr"
DEFAULT_PLUGINPATH="/lib/midimonster"
DEFAULT_CFGPATH="/etc/midimonster/midimonster.cfg"
DEFAULT_EXAMPLES="/share/midimonster"

############################################## FUNCTIONS ##############################################
assign_defaults(){
	VAR_PREFIX="${VAR_PREFIX:-$DEFAULT_PREFIX}"
	VAR_PLUGINS="${VAR_PLUGINS:-$VAR_PREFIX$DEFAULT_PLUGINPATH}"
	VAR_DEFAULT_CFG="${VAR_DEFAULT_CFG:-$DEFAULT_CFGPATH}"
	VAR_EXAMPLE_CFGS="${VAR_EXAMPLE_CFGS:-$VAR_PREFIX$DEFAULT_EXAMPLES}"
}

ARGS(){
	for i in "$@"; do
		case "$i" in
			--prefix=*)
				VAR_PREFIX="${i#*=}"
			;;
			--plugins=*)
				VAR_PLUGINS="${i#*=}"
			;;
			--defcfg=*)
				VAR_DEFAULT_CFG="${i#*=}"
			;;
			--examples=*)
				VAR_EXAMPLE_CFGS="${i#*=}"
			;;
			--dev)
				NIGHTLY=1
			;;
			-d|--default)
				assign_defaults
			;;
			-fu|--forceupdate)
				UPDATER_FORCE="1"
			;;
			--install-updater)
				NIGHTLY=1 prepare_repo
				install_script
				exit 0
			;;
			--install-dependencies)
				install_dependencies
				exit 0
			;;
			-h|--help|*)
				assign_defaults
				printf "${bold}Usage:${normal} ${0} ${c_green}[OPTIONS]${normal}"
				printf "\n\t${c_green}--prefix${normal} ${c_red}<path>${normal}\t\tSet the installation prefix\t\t${c_mag}Default:${normal} ${dim}%s${normal}" "$VAR_PREFIX"
				printf "\n\t${c_green}--plugins${normal} ${c_red}<path>${normal}\tSet the plugin install path\t\t${c_mag}Default:${normal} ${dim}%s${normal}" "$VAR_PLUGINS"
				printf "\n\t${c_green}--defcfg${normal} ${c_red}<path>${normal}\t\tSet the default configuration path\t${c_mag}Default:${normal} ${dim}%s${normal}" "$VAR_DEFAULT_CFG"
				printf "\n\t${c_green}--examples${normal} ${c_red}<path>${normal}\tSet the path for example configurations\t${c_mag}Default:${normal} ${dim}%s${normal}\n" "$VAR_EXAMPLE_CFGS"
				printf "\n\t${c_green}--dev${normal}\t\t\t\tInstall nightly version"
				printf "\n\t${c_green}-d, --default${normal}\t\t\tUse default values to install"
				printf "\n\t${c_green}-fu, --forceupdate${normal}\t\tForce the updater to update without a version check"
				printf "\n\t${c_green}--install-updater${normal}\t\tInstall the updater (Run with midimonster-updater)"
				printf "\n\t${c_green}--install-dependencies${normal}\t\tInstall dependencies and exit"
				printf "\n\t${c_green}-h, --help${normal}\t\t\tShow this message and exit"
				printf "\n\t${uline}${bold}${c_mag}Each argument can be overwritten by another, the last one is used!${normal}\n"
				rmdir "$tmp_path"
				exit 1
			;;
		esac
		shift
	done
}

# Install unmatched dependencies
install_dependencies(){
	for dependency in ${deps[@]}; do
		if [ "$(dpkg-query -W -f='${Status}' "$dependency" 2>/dev/null | grep -c "ok installed")" -eq 0 ]; then
			printf "Installing %s\n" "$dependency"
			apt-get install "$dependency"
		else
			printf "%s already installed!\n" "$dependency"
		fi
	done
	printf "\n"
}

ask_questions(){
	# Only say if necessary
	if [ -n "$VAR_PREFIX" ] || [ -n "$VAR_PLUGINS" ] || [ -n "$VAR_DEFAULT_CFG" ] || [ -n "$VAR_EXAMPLE_CFGS" ]; then
		printf "${bold}If you don't know what you're doing, just hit enter a few times.${normal}\n\n"
	fi
	
	if [ -z "$VAR_PREFIX" ]; then
		read -e -i "$DEFAULT_PREFIX" -p "PREFIX (Install root directory): " input
		VAR_PREFIX="${input:-$VAR_PREFIX}"
	fi

	if [ -z "$VAR_PLUGINS" ]; then
		read -e -i "$VAR_PREFIX$DEFAULT_PLUGINPATH" -p "PLUGINS (Plugin directory): " input
		VAR_PLUGINS="${input:-$VAR_PLUGINS}"
	fi

	if [ -z "$VAR_DEFAULT_CFG" ]; then
		read -e -i "$DEFAULT_CFGPATH" -p "Default config path: " input
		VAR_DEFAULT_CFG="${input:-$VAR_DEFAULT_CFG}"
	fi

	if [ -z "$VAR_EXAMPLE_CFGS" ]; then
		read -e -i "$VAR_PREFIX$DEFAULT_EXAMPLES" -p "Example config directory: " input
		VAR_EXAMPLE_CFGS="${input:-$VAR_EXAMPLE_CFGS}"
	fi
}

# Clone the repository and select the correct version
prepare_repo(){
	printf "Cloning the repository\n"
	git clone "https://github.com/cbdevnet/midimonster.git" "$tmp_path"

	# If not set via argument, ask whether to install development build
	if [ -z "$NIGHTLY" ]; then
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
				printf "${bold}Invalid input -- INSTALLING LATEST STABLE VERSION!${normal}\n"
				NIGHTLY=0
				;;
		esac
	fi

	# Roll back to last tag if a stable version was requested
	if [ "$NIGHTLY" != 1 ]; then
		cd "$tmp_path"
		printf "Finding latest stable version...\n"
		last_tag=$(git describe --abbrev=0)
		printf "Checking out %s...\n" "$last_tag"
		git checkout -f -q "$last_tag"
	fi
}

# Build and install the software
build(){
	# Export variables for make
	export PREFIX="$VAR_PREFIX"
	export PLUGINS="$VAR_PLUGINS"
	export DEFAULT_CFG="$VAR_DEFAULT_CFG"
	export EXAMPLES="$VAR_EXAMPLE_CFGS"

	cd "$tmp_path"
	make clean
	make "$makeargs"
	make install
}

# Save data for the updater
save_config(){
	rm -f "$updater_dir/updater.conf"
	mkdir -p "$updater_dir"	
	printf "Exporting updater config\n"
	printf "VAR_PREFIX=%s\nVAR_PLUGINS=%s\nVAR_DEFAULT_CFG=%s\nVAR_DESTDIR=%s\nVAR_EXAMPLE_CFGS=%s\n" "$VAR_PREFIX" "$VAR_PLUGINS" "$VAR_DEFAULT_CFG" "$VAR_DESTDIR" "$VAR_EXAMPLE_CFGS" > "$updater_dir/updater.conf"
}

# Updates this script using the one from the checked out repo (containing the requested version)
install_script(){
	mkdir -p "$updater_dir"
	printf "Copying updater to %s/updater.sh\n" "$updater_dir"
	cp "$tmp_path/installer.sh" "$updater_dir/updater.sh"
	chmod +x "$updater_dir/updater.sh"
	printf "Creating symlink /usr/bin/midimonster-updater\n"
	ln -s "$updater_dir/updater.sh" "/usr/bin/midimonster-updater"
}

error_handler(){
	printf "\nAborting\n"
	exit 1
}

cleanup(){
	if [ -d "$tmp_path" ]; then
		printf "Cleaning up temporary files...\n"
		rm -rf "$tmp_path"
	fi
}

################################################ Main #################################################
trap error_handler SIGINT SIGTERM
trap cleanup EXIT

# Parse arguments
ARGS "$@"
clear

# Check whether we have the privileges to install stuff
if [ "$user" != "root" ]; then
	printf "The installer/updater requires root privileges to install the midimonster system-wide\n"
	exit 1
fi

# Check if we can download the sources
if [ "$(wget -q --spider http://github.com)" ]; then
	printf "The installer/updater requires internet connectivity to download the midimonster sources\n"
	exit 1
fi

# Check whether the updater needs to run
if [ -f "$updater_dir/updater.conf" ] || [ "$UPDATER_FORCE" = "1" ]; then
	if [ -f "$updater_dir/updater.conf" ]; then
		. "$updater_dir/updater.conf"
		# Parse arguments again to compensate overwrite from source
		ARGS "$@"
		printf "Imported settings from %s/updater.conf\n" "$updater_dir"
	fi

	if [ -n "$UPDATER_FORCE" ]; then
		printf "Forcing the updater to start...\n\n"
	elif [ -x "$VAR_PREFIX/bin/midimonster" ]; then
		installed_version="$(midimonster --version)"
		if [[ "$installed_version" =~ "$latest_version" ]]; then
			printf "The installed version ${bold}$installed_version${normal} seems to be up to date\nDoing nothing\n\n"
			exit 0
		else
			printf "The installed version ${bold}$installed_version${normal} does not match the latest version ${bold}$latest_version${normal}\nMaybe you are running a development version?\n\n"
		fi
	fi

	# Run updater steps
	prepare_repo
	install_script
	save_config
	build
else
	# Run installer steps
	install_dependencies
	prepare_repo
	ask_questions
	install_script
	save_config
	build
fi
exit 0

