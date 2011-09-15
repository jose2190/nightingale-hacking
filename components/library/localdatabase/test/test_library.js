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
 * \brief Test file
 */

function runTest () {

  var SB_NS = "http://songbirdnest.com/data/1.0#";
  var ios = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);

  var databaseGUID = "test_library";
  var library = createLibrary(databaseGUID);

  // test create
  var uriSpec = "file:///foo";
  var uri = ios.newURI(uriSpec, null, null);
  
  var item1 = library.createMediaItem(uri);
  var now = new Date();
  assertEqual(item1.getProperty(SB_NS + "contentURL"), uriSpec);

  var created = new Date(parseInt(item1.getProperty(SB_NS + "created")));
  var updated = new Date(parseInt(item1.getProperty(SB_NS + "updated")));

  assertEqual(created.getTime() == updated.getTime(), true);
  // Compare the dates with a little slack -- they should be very close
  assertEqual(now - created < 5000, true);
  assertEqual(now - updated < 5000, true);

  // Make sure we are getting different guids
  var item2 = library.createMediaItem(uri, null, true);
  assertNotEqual(item1.guid, item2.guid);

  // Test that they items were added to the library view the view list
  assertEqual(library.getItemByGuid(item1.guid).guid, item1.guid);
  assertEqual(library.getItemByGuid(item2.guid).guid, item2.guid);
  
  var listListener = new TestMediaListListener();
  library.addListener(listListener, false);

  var uri2 = ios.newURI("file:///bar", null, null);
  var item3 = library.createMediaItem(uri2, null, true);

  assertTrue(item3.equals(listListener.added[0].item));
  
  library.remove(item1);
  assertTrue(item1.equals(listListener.removedBefore[0].item));
  assertTrue(item1.equals(listListener.removedAfter[0].item));
  
  // test createMediaItemIfNotExist
  var item4 = {};
  var hasItem4 = library.createMediaItemIfNotExist(uri2, null, item4);
  assertFalse(hasItem4);
  assertTrue(item4.value.equals(item3));
  var uri3 = ios.newURI("file:///quux", null, null);
  hasItem4 = library.createMediaItemIfNotExist(uri3, null, item4);
  assertTrue(hasItem4);
  assertFalse(item4.value.equals(item3));
  library.remove(item4.value);
  
  // Test if removing items from the library also remove items from the
  // playlist.
  var enumerationListener = new TestMediaListEnumerationListener();
  
  var list = library.getMediaItem("7e8dcc95-7a1d-4bb3-9b14-d4906a9952cb");
  assertEqual(list.length, 20);
  
  list.enumerateAllItems(enumerationListener,
                         Ci.sbIMediaList.ENUMERATIONTYPE_SNAPSHOT);
  
  var listCount = enumerationListener.count;
  assertTrue(listCount > 0);
  
  listListener.reset();
  var removedItemCount = 0;
  listListener.onAfterItemRemoved = function onAfterItemRemoved(list, item) {
    removedItemCount++;
  }
  
  library.removeSome(enumerationListener.QueryInterface(Ci.nsISimpleEnumerator));
  
  assertEqual(removedItemCount, listCount);
  
  assertEqual(list.length, 0);
  
  library.removeListener(listListener);

  // Now test if clearing the library also clears the playlist.
  library = createLibrary(databaseGUID);
  list = library.getMediaItem("7e8dcc95-7a1d-4bb3-9b14-d4906a9952cb");
  
  assertEqual(list.length, 20);
  
  library.clear();
  
  assertEqual(list.length, 0);
  // Test that modifying a media item notifies the library and playlist.
  library = createLibrary(databaseGUID);
  list = library.getMediaItem("7e8dcc95-7a1d-4bb3-9b14-d4906a9952cb");
  var item = list.getItemByIndex(8);
  
  listListener = new TestMediaListListener();
  list.addListener(listListener, false);
  
  var libraryListener = new TestMediaListListener();
  library.addListener(libraryListener, false);

  item.contentType = "foo/foo";
  
  assertEqual(listListener.updatedItem, item);
  assertEqual(libraryListener.updatedItem, item);
  
  list.removeListener(listListener);
  library.removeListener(libraryListener);

  // Test that modifying an item doesn't notify lists that don't contain the
  // item.
  library = createLibrary(databaseGUID);
  list = library.getMediaItem("7e8dcc95-7a1d-4bb3-9b14-d4906a9952cb");
  
  listListener = new TestMediaListListener();
  list.addListener(listListener, false);
  
  item = library.getItemByGuid("3E63F4C2-AD99-11DB-9321-C22AB7121F49");
  
  libraryListener = new TestMediaListListener();
  library.addListener(libraryListener, false);
  
  item.contentType = "foo/foo";
  
  assertNotEqual(listListener.updatedItem, item);
  assertEqual(libraryListener.updatedItem, item);

  list.removeListener(listListener);
  library.removeListener(libraryListener);

  // Test create with properties
  var props = createPropertyArray();
  props.appendProperty(SB_NS + "artistName", "a-ha");
  props.appendProperty(SB_NS + "albumName", "Back in Black");
  props.appendProperty(SB_NS + "contentLength", "123");
  props.appendProperty(SB_NS + "contentURL", "http://www.foo.com/foo.mp3");
  var item4 = library.createMediaItem(uri, props);

  // contentURL should not have been affected by the property list
  assertEqual(item4.getProperty(SB_NS + "contentURL"), uriSpec);
  assertEqual(item4.getProperty(SB_NS + "artistName"), "a-ha");
  assertEqual(item4.getProperty(SB_NS + "albumName"), "Back in Black");
  assertEqual(item4.getProperty(SB_NS + "contentLength"), "123");

  // Make sure that items can't be resurrected once removed.
  var newItem = library.createMediaItem(uri, null, true);
  var guid = newItem.guid;

  library.remove(newItem);

  var exceptionResult;
  try {
    var item = library.getItemByGuid(guid);
  }
  catch (e) {
    exceptionResult = e.result;
  }
  assertEqual(exceptionResult, Cr.NS_ERROR_NOT_AVAILABLE);

  // Removing a removed item should do nothing
  library.remove(newItem);

  var databaseGUID = "test_library_clearItems";
  var library = createLibrary(databaseGUID);

  var list = library.createMediaList("simple");
  var itemFoo = library.createMediaItem(uri, null, true);
  var itemBar = library.createMediaItem(uri2, null, true);
  
  var itemFooGUID = itemFoo.guid;
  
  list.add(itemFoo);
  list.add(itemBar);
  assertEqual(list.length, 2);
  library.clearItems();
  assertEqual(list.length, 0);
  
  try {
    let deadItemFoo = library.getMediaItem(itemFooGUID);
    throw "ERROR! Dead Item in mMediaItemInfoTable!";
  }
  catch(e) {
    // Everything is fine if we get NS_ERROR_NOT_AVAILABLE.
    // Other exceptions are bad.
    if(e.result != Cr.NS_ERROR_NOT_AVAILABLE) {
      throw e;
    }
  }

  var itemFoo2 = library.createMediaItem(uri, null, true);
  var itemBar2 = library.createMediaItem(uri2, null, true);
  
  itemFoo2.setProperty(SB_NS + "contentType", "video");
  itemBar2.setProperty(SB_NS + "contentType", "audio");
  
  list.add(itemFoo2);
  list.add(itemBar2);
  assertEqual(list.length, 2);
  library.clearItemsByType("audio");
  assertEqual(list.length, 1);
}

function createPropertyArray() {
  return Cc["@songbirdnest.com/Songbird/Properties/MutablePropertyArray;1"]
           .createInstance(Ci.sbIMutablePropertyArray);
}

