/*
//
// BEGIN SONGBIRD GPL
//
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2008 POTI, Inc.
// http://songbirdnest.com
//
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
//
// Software distributed under the License is distributed
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either
// express or implied. See the GPL for the specific language
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
// END SONGBIRD GPL
//
*/

/**
 * \brief Some globally useful stuff for the library tests
 */

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

function readList(dataFile) {

  var data = readFile(dataFile);
  var a = data.split("\n");

  var b = [];
  for(var i = 0; i < a.length - 1; i++) {
    b.push(a[i].split("\t")[0]);
  }

  return b;
}

function getFile(fileName) {
  var file = Cc["@mozilla.org/file/directory_service;1"]
               .getService(Ci.nsIProperties)
               .get("resource:app", Ci.nsIFile);
  file = file.clone();
  file.append("testharness");
  file.append("localdatabaselibrary");
  file.append(fileName);
  return file;
}

function BatchEndListener() {
  this.depth = 0;
}
BatchEndListener.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.sbIMediaListListener]),
  onItemAdded: function() true,
  onBeforeItemRemoved: function() true,
  onAfterItemRemoved: function() true,
  onItemUpdated: function() true,
  onItemMoved: function() true,
  onBeforeListCleared: function() true,
  onListCleared: function() true,
  onBatchBegin: function(aMediaList) {
    ++this.depth;
    log("depth: " + this.depth);
  },
  onBatchEnd: function(aMediaList) {
    --this.depth;
    log("depth: " + this.depth);
  },
  waitForCompletion: function(aCallback) {
    var targetDepth = this.depth;
    aCallback();
    log("waiting for depth " + this.depth + " to go to " + targetDepth);
    while (this.depth > targetDepth) {
      sleep(100);
    }
  },
  waitForEndBatch: function() {
    log("waiting for depth " + this.depth + " to go straight to 0");
    while (this.depth > 0) {
      sleep(100);
    }
  }
};
