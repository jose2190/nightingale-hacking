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

  Components.utils.import("resource://app/jsmodules/sbProperties.jsm");
  Components.utils.import("resource://app/jsmodules/sbLibraryUtils.jsm");

  var library = createLibrary("test_viewandcfs", null, false);
  library.clear();

  var playlist1 = library.createMediaList("simple");
  var playlist2 = library.createMediaList("simple");

  var items = [
    ["The Beatles", "Abbey Road", "Come Together",  "ROCK"],
    ["The Beatles", "Abbey Road", "Sun King",       "ROCK"],
    ["The Beatles", "Let It Be",  "Get Back",       "POP"],
    ["The Beatles", "Let It Be",  "Two Of Us",      "POP"],
    ["The Doors",   "L.A. Woman", "L.A. Woman",     "ROCK"],
    ["The Doors",   "L.A. Woman", "Love Her Madly", "ROCK"]
  ];

  function makeItem(i) {
    var item = library.createMediaItem(
      newURI("http://foo/" + i + ".mp3"),
      SBProperties.createArray([
        [SBProperties.artistName,items[i][0]],
        [SBProperties.albumName, items[i][1]],
        [SBProperties.trackName, items[i][2]],
        [SBProperties.genre,     items[i][3]]
     ]));
    return item;
  }

  var item1 = makeItem(0);
  var item2 = makeItem(1);
  var item3 = makeItem(2);
  var item4 = makeItem(3);
  var item5 = makeItem(4);
  var item6 = makeItem(5);

  var view = library.createView();
  var cfs = view.cascadeFilterSet;

  assertView(view, [
    playlist1,
    playlist2,
    item1,
    item2,
    item3,
    item4,
    item5,
    item6
  ]);


  assertEqual(view.searchConstraint, null);
  assertEqual(view.filterConstraint, null);

  var filter = LibraryUtils.createConstraint([
    [
      [SBProperties.isList, ["0"]]
    ],
    [
      [SBProperties.hidden, ["0"]]
    ]
  ]);

  // Set up root libtary display filters
  view.filterConstraint = filter;

  // Set up cfs
  cfs.appendSearch(["*"], 1);
  cfs.appendFilter(SBProperties.genre);
  cfs.appendFilter(SBProperties.artistName);
  cfs.appendFilter(SBProperties.albumName);

  assertView(view, [
    item1,
    item2,
    item3,
    item4,
    item5,
    item6
  ]);

  assertEqual(view.searchConstraint, null);
  assertTrue(view.filterConstraint.equals(filter));

  // Set a filter
  cfs.set(1, ["ROCK"], 1);

  assertView(view, [
    item1,
    item2,
    item5,
    item6
  ]);

  assertEqual(view.searchConstraint, null);
  assertTrue(view.filterConstraint.equals(LibraryUtils.createConstraint([
    [
      [SBProperties.isList, ["0"]]
    ],
    [
      [SBProperties.hidden, ["0"]]
    ],
    [
      [SBProperties.genre, ["ROCK"]]
    ]
  ])));

  cfs.set(2, ["The Beatles"], 1);

  assertView(view, [
    item1,
    item2
  ]);

  assertEqual(view.searchConstraint, null);
  assertTrue(view.filterConstraint.equals(LibraryUtils.createConstraint([
    [
      [SBProperties.isList, ["0"]]
    ],
    [
      [SBProperties.hidden, ["0"]]
    ],
    [
      [SBProperties.genre, ["ROCK"]]
    ],
    [
      [SBProperties.artistName, ["The Beatles"]]
    ]
  ])));

  // Clear the filter set.  This should revert to the root library display
  // filters
  cfs.clearAll();

  assertView(view, [
    item1,
    item2,
    item3,
    item4,
    item5,
    item6
  ]);

  assertEqual(view.searchConstraint, null);
  assertTrue(view.filterConstraint.equals(filter));
  // Set a search
  cfs.set(0, ["Beat"], 1);

  assertView(view, [
    item1,
    item2,
    item3,
    item4
  ]);

  assertTrue(view.searchConstraint.equals(LibraryUtils.createConstraint([
    [
      ["*", ["Beat"]]
    ]
  ])));
  assertTrue(view.filterConstraint.equals(filter));

  // Clearing the filters and searches on the view should remove all filters
  // and views
  view.filterConstraint = null;

  assertView(view, [
    item1,
    item2,
    item3,
    item4
  ]);

  assertTrue(view.searchConstraint.equals(LibraryUtils.createConstraint([
    [
      ["*", ["Beat"]]
    ]
  ])));
  assertEqual(view.filterConstraint, null);

  view.searchConstraint = null;

  assertView(view, [
    playlist1,
    playlist2,
    item1,
    item2,
    item3,
    item4,
    item5,
    item6
  ]);

  assertEqual(view.searchConstraint, null);
  assertEqual(view.filterConstraint, null);
}

function assertView(view, list) {
  if (view.length != list.length) {
    fail("View length not equal to list length, " + view.length +
         " != " + list.length);
  }

  for(var i = 0; i < view.length; i++) {
    if (!view.getItemByIndex(i).equals(list[i])) {
      fail("View is different than list at index " + i + ", " +
           view.getItemByIndex(i).guid + " != " + list[i].guid);
    }
  }

}
