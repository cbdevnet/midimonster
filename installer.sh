#!/bin/bash

################################################ SETUP ################################################
deps=(libasound2-dev libevdev-dev liblua5.3-dev libjack-jackd2-dev pkg-config libssl-dev gcc make wget git)
user=$(whoami)                  # for bypassing user check replace "$(whoami)" with "root".

tmp_path=$(mktemp -d)           # Repo download path
updater_dir=/etc/midimonster-updater-installer       # Updater download + config path
updater_file=$updater_dir/updater.conf                   # 

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
    if [ $(dpkg-query -W -f='${Status}' $t 2>/dev/null | grep -c "ok installed") -eq 0 ];
    then
        printf "\nInstalling "$t"";
        apt-get install $t;
        printf "\nDone.";
    else
        printf "\n"$t" already installed!"
    fi
done
printf "\n"
}

INSTALL-PREP () {
(#### Subshell make things like cd $tmp_path easier to revert
    printf "\nStarting download..."
    git clone https://github.com/cbdevnet/midimonster.git "$tmp_path" # Gets Midimonster   
    printf "\n\n\nInitializing repository..."
    cd $tmp_path
    git init $tmp_path
    printf "\n"
    
    read -p "Do you want to install the nightly version? (y/n)? " magic      #Asks for nightly version
    case "$magic" in 
    y|Y )   printf "\nOK! You´re a risky person ;D"
            NIGHTLY=1
            ;;
    n|N )   printf "\nThat´s ok I´ll install the latest stable version for you ;-)"
            NIGHTLY=0
            ;;
      * )   printf "\nInvalid input"
            ERROR
            ;;
    esac

    if [ $NIGHTLY != 1 ]; then printf "\nFinding latest stable version..."; Iversion=$(git describe --abbrev=0); printf "\nStarting Git checkout to "$Iversion"..."; git checkout -f -q $Iversion; fi # Git checkout if NIGHTLY !=1
    printf "\nPreparing Done.\n\n\n"
)
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
(#### Subshell make things like cd $tmp_path easier to revert


    printf "\nStarting download..."
    git clone https://github.com/cbdevnet/midimonster.git "$tmp_path" # Gets Midimonster   
    printf "\n\n"
    printf "\nInitializing repository..."
    cd $tmp_path
    git init $tmp_path
    printf "\n"

    read -p "Do you want to install the nightly version? (y/n)? " magic      #Asks for nightly version
    case "$magic" in 
    y|Y )   printf "\nOK! You´re a risky person ;D"
            NIGHTLY=1
            ;;
    n|N )   printf "\nThat´s ok I´ll install the latest stable version for you ;-)"
            NIGHTLY=0
            ;;
      * )   printf "\nInvalid input"
            ERROR
            ;;
    esac

    if [ $NIGHTLY != 1 ]; then printf "\nFinding latest stable version..."; Iversion=$(git describe --abbrev=0); printf "\nStarting Git checkout to "$Iversion"..."; git checkout -f -q $Iversion; fi # Git checkout if NIGHTLY !=1
    printf "\nDone.\n\n\n"
 )


rm -f "$VAR_PREFIX/bin/midimonster"
rm -rf "$VAR_PLUGINS/"

UPDATER_SAVE

export PREFIX=$VAR_PREFIX
export PLUGINS=$VAR_PLUGINS
export DEFAULT_CFG=$VAR_DEFAULT_CFG
export DESTDIR=$VAR_DESTDIR
export EXAMPLES=$VAR_EXAMPLE_CFGS

printf "\nSucessfully imported Updater settings from $updater_file."
}

UPDATER () {
    installed_version="$(midimonster --version)"
    #installed_version="MIDIMonster v0.3-40-gafed325" # FOR TESTING ONLY! (or bypassing updater version check)
    if [[ "$installed_version" =~ "$latest_version" ]]; then
        printf "\nNewest Version is already installed! ${bold}($installed_version)${normal}\n\n"
        ERROR
    else
        printf "\nThe installed Version ${bold}´$installed_version´${normal} equals not the newest stable version ${bold}´$latest_version´${normal} ( Maybe you are running on nightly? )\n\n"
    fi

    UPDATER-PREP
    INSTALL-RUN

    printf "\nUpdating updater/installer script in $updater_dir"
    wget "https://raw.githubusercontent.com/cbdevnet/midimonster/master/installer.sh" -O $updater_dir
    chmod +x $updater_dir/installer.sh
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
    printf "\nSaving updater to $updater_dir/installer.sh" 
    mkdir -p "$updater_dir"
    wget https://raw.githubusercontent.com/cbdevnet/midimonster/master/installer.sh -O $updater_dir/installer.sh
    printf "\nCreating symlink to updater/installer in /usr/bin/midimonster-updater-installer"
    ln -s "$updater_dir/installer.sh" "/usr/bin/midimonster-updater-installer"
    chmod +x "$updater_dir/installer.sh"
    printf "\nExporting updater config to $updater_file"
    printf "VAR_PREFIX=%s\nVAR_PLUGINS=%s\nVAR_DEFAULT_CFG=%s\nVAR_DESTDIR=%s\nVAR_EXAMPLE_CFGS=%s\n" "$VAR_PREFIX" "$VAR_PLUGINS" "$VAR_DEFAULT_CFG" "$VAR_DESTDIR" "$VAR_EXAMPLE_CFGS" > $updater_file
}

ERROR () {
    printf "\nAborting..."
    CLEAN
    printf "Exiting...\n\n\n"
    exit 1
}

DONE () {
    printf "\n\nDone."
    CLEAN
    exit 0
}

CLEAN () {
    printf "\nCleaning...\n\n"
    rm -rf $tmp_path
}

############################################## FUNCTIONS ##############################################


################################################ Main #################################################
trap ERROR SIGINT SIGTERM SIGKILL
clear

if [ "$user" != "root" ]; then printf "\nInstaller must be run as root"; ERROR; fi    # Check if $user = root!
if [ $(wget -q --spider http://github.com) $? -eq 1 ]; then printf "\nYou need connection to the internet"; ERROR; fi

if [ -f $updater_file ] # Checks if updater config file exist and import it(overwrite default values!)
then
    
    printf "\nStarting updater...\n\n"
    . $updater_file

    if [ -x "$VAR_PREFIX/bin/midimonster" ] # Check if binary $updater/bin/midimonster exist. If yes start updater else skip.
    then
        UPDATER
    else
        printf "\nMidimonster binary not found, skipping updater.\n"
    fi

fi

INSTALL-DEPS
INSTALL-PREP
printf "\n"
INSTALL-RUN
DONE