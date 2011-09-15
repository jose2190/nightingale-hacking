#! /bin/sh
#
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is mozilla.org code.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 1999
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

# allmakefiles.sh - List of all makefiles. 
#   Appends the list of makefiles to the variable, MAKEFILES.
#   There is no need to rerun autoconf after adding makefiles.
#   You only need to run configure.
#
#   Please keep the modules in this file in sync with those in
#   mozilla/build/unix/modules.mk
#

MAKEFILES=""

# add_makefiles - Shell function to add makefiles to MAKEFILES
add_makefiles() {
    MAKEFILES="$MAKEFILES $*"
}

if [ "$srcdir" = "" ]; then
    srcdir=.
fi



#
# The following folders will be recursively scanned, and all Makefile.in files 
# will be included in $MAKEFILES
#
SCANNED_MAKEFILE_DIRS="
app
branding
bindings
components
documentation
extensions
extras
feathers
installer
locales
tools
tools/mochitest
update
"

echo "allmakefiles.sh: finding makefiles..."
cd ../
# Find all Makefile.in file paths, then strip the leading ./ and the trailing .in
MAKEFILES_auto=`find $SCANNED_MAKEFILE_DIRS -name .svn -prune -or -name Makefile.in -print | perl -pe "s/^\.\///; s/\.in\n\$/ /;"`
cd compiled
echo "allmakefiles.sh: done"


#
# Additional makefile locations not included in the scan
#

MAKEFILES_app="
Makefile
"

MAKEFILES_build="
build/autodefs.mk
build/Makefile
"

MAKEFILES_dependencies="
dependencies/Makefile
dependencies/vendor/mozbrowser/Makefile
dependencies/vendor/mozbrowser/components/preferences/Makefile
dependencies/vendor/mozbrowser/fuel/Makefile
dependencies/vendor/mozbrowser/fuel/public/Makefile
dependencies/vendor/mozbrowser/fuel/src/Makefile
dependencies/vendor/mozbrowser/locales/Makefile
dependencies/vendor/mozbrowser/themes/Makefile
dependencies/vendor/mozbrowser/themes/pinstripe/browser/Makefile
dependencies/vendor/mozbrowser/themes/winstripe/browser/Makefile
dependencies/vendor/mozbrowser/components/search/Makefile
dependencies/vendor/mozbrowser/components/sidebar/src/Makefile
dependencies/vendor/mozbrowser/components/Makefile
dependencies/vendor/mozbrowser/base/Makefile
"


#
# Put it all together
#

add_makefiles "
$MAKEFILES_auto
$MAKEFILES_app
$MAKEFILES_build
$MAKEFILES_dependencies
"

