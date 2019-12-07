#!/bin/bash

################################################ SETUP ################################################
deps=(libasound2-dev libevdev-dev liblua5.3-dev libjack-jackd2-dev pkg-config libssl-dev gcc make wget git)
user=$(whoami)                  # for bypassing user check replace "$(whoami)" with "root".

script_path="`cd $0; pwd`"      # Script dir
tmp_path=$(mktemp -d)           # Repo download path

Iversion="v0.2"                 # (fallback version if )
makeargs=all                    # Build args

VAR_DESTDIR=""                  # Unused
VAR_PREFIX="/usr"
VAR_PLUGINS="$VAR_PREFIX/lib/midimonster"
VAR_DEFAULT_CFG="/etc/midimonster/midimonster.cfg"
VAR_EXAMPLE_CFGS="$VAR_PREFIX/share/midimonster"

################################################ SETUP ################################################

############################################## FUNCTIONS ##############################################

INSTALL-DEPS () {           ##Install deps from array "$deps"
for t in ${deps[@]}; do
    if [ $(dpkg-query -W -f='${Status}' $t 2>/dev/null | grep -c "ok installed") -eq 0 ];
    then
        echo "Installing "$t"";
        apt-get install $t;
        echo "Done.";
    else
        echo ""$t" already installed!"

    fi
done
echo ""
}

INSTALL-PREP () {
    echo "Starting Git!"
    git clone https://github.com/cbdevnet/midimonster.git "$tmp_path" # Gets Midimonster
    Iversion=(git describe --abbrev=0)                            # Get last tag(stable version)
    echo "Starting Git checkout to "$Iversion""
    git init $tmp_path
    (cd $tmp_path; git checkout $Iversion)
    echo ""

    read -e -i "$VAR_PREFIX" -p "PREFIX (Install root directory): " input # Reads VAR_PREFIX
    VAR_PREFIX="${input:-$VAR_PREFIX}"

    read -e -i "$VAR_PLUGINS" -p "PLUGINS (Plugin directory): " input # Reads VAR_PLUGINS
    VAR_PLUGINS="${input:-$VAR_PLUGINS}"

    read -e -i "$VAR_DEFAULT_CFG" -p "Default config path: " input # Reads VAR_DEFAULT_CFG
    VAR_DEFAULT_CFG="${input:-$VAR_DEFAULT_CFG}"

    read -e -i "$VAR_EXAMPLE_CFGS" -p "Example config directory: " input # Reads VAR_EXAMPLE_CFGS
    VAR_EXAMPLE_CFGS="${input:-$VAR_EXAMPLE_CFGS}"


    export PREFIX=$VAR_PREFIX
    export PLUGINS=$VAR_PLUGINS
    export DEFAULT_CFG=$VAR_DEFAULT_CFG
    export DESTDIR=$VAR_DESTDIR
    export EXAMPLES=$VAR_EXAMPLE_CFGS
}

INSTALL-RUN () {                                    # Build
    cd "$tmp_path"
    make clean
    make $makeargs
    make install
}

ERROR () {
    echo "Aborting..."
    CLEAN
    exit 1
}

DONE () {
    echo Done.
    CLEAN
    exit 0
}

CLEAN () {
    echo "Cleaning..."
    rm -rf $tmp_path
}

############################################## FUNCTIONS ##############################################


################################################ Main #################################################

trap ERROR SIGINT SIGTERM SIGKILL
clear

if [ $user != "root" ]; then echo "Installer must be run as root"; ERROR; fi    # Check if $user = root!

if [ $(wget -q --spider http://github.com) $? -eq 0 ]; then "INSTALL-DEPS"; else echo You need connection to the internet; ERROR ; fi

INSTALL-PREP
echo ""
INSTALL-RUN
DONE