/*
 *=BEGIN SONGBIRD GPL
 *
 * This file is part of the Songbird web player.
 *
 * Copyright(c) 2005-2009 POTI, Inc.
 * http://www.songbirdnest.com
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the ``GPL'').
 *
 * Software distributed under the License is distributed
 * on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *=END SONGBIRD GPL
 */

var gHotkeysPane = {

  _list: null,
  _remove: null,
  _set: null,
  _add: null,
  _actionlist: null,
  _hotkey: null,
  _tool: null,
  _binding_enabled: null,
  _enabled: null,
  _actions: null,
  _hotkeylabel: null,
  _actionlabel: null,
  _disabledHotkeyList: null,

  init: function init()
  {
    var jsLoader = Components.classes["@mozilla.org/moz/jssubscript-loader;1"].getService(Components.interfaces.mozIJSSubScriptLoader);
    jsLoader.loadSubScript( "chrome://songbird/content/scripts/messageBox.js", this );

    window.addEventListener("unload", onHotkeysUnload, true);

    this._hotkeyService = Cc["@songbirdnest.com/Songbird/HotkeyService;1"]
                            .getService(Ci.sbIHotkeyService);

    this._binding_enabled = SBDataBindElementAttribute(this._hotkeyService.hotkeysEnabledDRKey, "hotkeys.enabled", "checked", true);

    this._list = document.getElementById("hotkey.list");
    this._add = document.getElementById("hotkey.add");
    this._set = document.getElementById("hotkey.set");
    this._remove = document.getElementById("hotkey.remove");
    this._actionlist = document.getElementById("hotkey.actions");
    this._hotkey = document.getElementById("hotkey.hotkey");
    this._tool = document.getElementById("hotkey-hotkeytool");
    this._hotkeylabel = document.getElementById("hotkey.hotkeylabel");
    this._actionlabel = document.getElementById("hotkey.actionlabel");
    this._enabled = document.getElementById("hotkeys.enabled");
    this._disabledHotkeyList = [];

    this.loadActions();
    this.loadHotkeys();
    this.enableDisableElements();
  },

  onUnload: function()
  {
    window.removeEventListener("unload", onHotkeysUnload, true);
    this._binding_enabled.unbind();
    this._binding_enabled = null;
  },

  loadActions: function()
  {
    var menupopup = this._actionlist.firstChild;
    while (menupopup.childNodes.length>0) menupopup.removeChild(menupopup.childNodes[0]);

    var hotkeyActionsComponent = Components.classes["@songbirdnest.com/Songbird/HotkeyActions;1"];
    if (hotkeyActionsComponent) this._actions = hotkeyActionsComponent.getService(Components.interfaces.sbIHotkeyActions);
    if (this._actions)
    {
      for (var i=0;i<this._actions.bundleCount;i++)
      {
        var bundle = this._actions.enumBundle(i);
        for (var j=0;j<bundle.actionCount;j++)
        {
          var actionid = bundle.enumActionID(j);
          var actiondesc = bundle.enumActionLocaleDescription(j);
          var item = document.createElement("menuitem");
          item.actionid = actionid;
          item.setAttribute("label", actiondesc);
          menupopup.appendChild(item);
        }
      }
      this._actionlist.selectedItem = menupopup.childNodes[0];
    }
  },

  loadHotkeys: function()
  {
    var hotkeyConfigList = this._hotkeyService.getHotkeys();
    for (var i = 0; i < hotkeyConfigList.length; i++) {
      // Read hotkey config
      var hotkeyConfig =
            hotkeyConfigList.queryElementAt(i, Ci.sbIHotkeyConfiguration);
      if (!this._hotkeyDisabled(hotkeyConfig)) {
        var keycombo = hotkeyConfig.key;
        var keydisplay = hotkeyConfig.keyReadable;
        var actionid = hotkeyConfig.action;
        var action = this._getLocalizedAction(actionid);
        // make list items accordingly
        this._addItem(keycombo, keydisplay, actionid, action, -1);
      }
      else {
        this._disabledHotkeyList.push(hotkeyConfig);
      }
    }
    this.updateButtons();
  },

  _hotkeyDisabled: function(hotkeyConfig) {
    // Hotkey is disabled if it doesn't have an action.  This might happen if an
    // extension sets up a hotkey and is then disabled.
    var nodes = this._actionlist.firstChild.childNodes;
    for (var i=0;i<nodes.length;i++) {
      if (hotkeyConfig.action == nodes[i].actionid)
        return false;
    }

    return true;
  },

  _getLocalizedAction: function(actionid)
  {
    var nodes = this._actionlist.firstChild.childNodes;
    for (var i=0;i<nodes.length;i++) {
      if (actionid == nodes[i].actionid) return nodes[i].getAttribute("label");
    }
    return actionid;
  },

  _addItem: function(keycombo, keydisplay, actionid, action, replace)
  {
    this._tool.setHotkey(keycombo, keydisplay);
    keydisplay = this._tool.getHotkey(1);

    // if an index to replace was specified, figure out the corresponding item to use as a marker for insertBefore
    var before = (replace == -1) ? null : this._list.getItemAtIndex(replace);
    // make list item
    var listitem = document.createElement("listitem");
    listitem.keycombo = keycombo;
    listitem.actionid = actionid;
    var listcell_action = document.createElement("listcell");
    listcell_action.setAttribute("label", action);
    var listcell_keydisplay = document.createElement("listcell");
    listcell_keydisplay.setAttribute("label", keydisplay.toUpperCase());
    listitem.appendChild(listcell_action);
    listitem.appendChild(listcell_keydisplay);
    if (before) {
      // insert the item at the correct spot
      this._list.insertBefore(listitem, before);
      // and remove the item we were supposed to replace
      this._list.removeChild(before);
    } else {
      // append at the end of the list (replace was -1)
      this._list.appendChild(listitem);
    }
  },

  saveHotkeys: function()
  {
    // Set all hotkeys, extract them from the list itself
    var hotkeyConfigList = Cc["@songbirdnest.com/moz/xpcom/threadsafe-array;1"]
                             .createInstance(Ci.nsIMutableArray);
    var n = this._list.getRowCount();
    for (var i=0;i<n;i++)
    {
      var hotkeyConfig = Cc["@songbirdnest.com/Songbird/HotkeyConfiguration;1"]
                           .createInstance(Ci.sbIHotkeyConfiguration);
      var item = this._list.getItemAtIndex(i);
      var keycombo = item.keycombo;
      var actionid = item.actionid;
      var actioncell = item.firstChild;
      var keydisplaycell = actioncell.nextSibling;
      var keydisplay = keydisplaycell.getAttribute("label");
      hotkeyConfig.key = keycombo;
      hotkeyConfig.keyReadable = keydisplay;
      hotkeyConfig.action = actionid;
      if (!this._configListContainsKey(hotkeyConfigList, hotkeyConfig.key))
        hotkeyConfigList.appendElement(hotkeyConfig, false);
    }

    // Add any disabled hotkeys.  This ensures that if an extension adds a
    // hotkey and is then disabled, its hotkey setting is preserved when the
    // extension is re-enabled.  If the user reassigns the disabled hotkey in
    // the UI, it will not be added here.
    if (this._disabledHotkeyList) {
      for (var i = 0; i < this._disabledHotkeyList.length; i++) {
        if (!this._configListContainsKey(hotkeyConfigList, hotkeyConfig.key))
          hotkeyConfigList.appendElement(this._disabledHotkeyList[i], false);
      }
    }

    this._hotkeyService.setHotkeys(hotkeyConfigList);
  },

  _configListContainsKey: function(aHotkeyConfigList, aKey) {
    for (var i = 0; i < aHotkeyConfigList.length; i++) {
      var hotkeyConfig =
            aHotkeyConfigList.queryElementAt(i, Ci.sbIHotkeyConfiguration);
      if (hotkeyConfig.key == aKey)
        return true;
    }

    return false;
  },

  onSelectHotkey: function()
  {
    this.updateButtons();
    var selected = this._list.selectedIndex;
    if (selected >= 0) {
      var item = this._list.getItemAtIndex(selected);
      var actioncell = item.firstChild;
      var keydisplaycell = actioncell.nextSibling;
      // load action in menulist
      var nodes = this._actionlist.firstChild.childNodes;
      var actionstr = actioncell.getAttribute("label");
      for (var i=0;i<nodes.length;i++) {
        if (nodes[i].getAttribute("label") == actionstr) {
          this._actionlist.selectedItem = nodes[i]
          break;
        }
      }
      // load hotkey in hotkey control
      this._hotkey.setHotkey(item.keycombo, keydisplaycell.getAttribute("label"));
    }
  },

  updateButtons: function()
  {
    // disable set & remove when no item is selected
    var disabled = (this._list.selectedIndex== -1);
    var alldisabled = !this._hotkeyService.hotkeysEnabled;
    this._remove.setAttribute("disabled", (disabled||alldisabled));
    this._set.setAttribute("disabled", (disabled||alldisabled));
    this._add.setAttribute("disabled", alldisabled);
  },

  addHotkey: function()
  {
    // add the hotkey to the list
    var keycombo = this._hotkey.getHotkey(false);
    if (this._checkComboExists(keycombo, -1)) return;
    var keydisplay = this._hotkey.getHotkey(true);
    var action = this._actionlist.selectedItem.getAttribute("label");
    var actionid = this._actionlist.selectedItem.actionid;
    if (action == "" || keycombo == "" || keydisplay == "" || actionid == "") return;
    this._addItem(keycombo, keydisplay, actionid, action, -1);
    // select the last (newly added) item
    this._list.selectedIndex = this._list.getRowCount()-1;
    this.saveHotkeys();
  },

  setHotkey: function()
  {
    // change the hotkey item that's currently selected
    var selected = this._list.selectedIndex;
    var keycombo = this._hotkey.getHotkey(false);
    if (this._checkComboExists(keycombo, selected)) return;
    var keydisplay = this._hotkey.getHotkey(true);
    var action = this._actionlist.selectedItem.getAttribute("label");
    var actionid = this._actionlist.selectedItem.actionid;
    if (action == "" || keycombo == "" || keydisplay == "") return;
    this._addItem(keycombo, keydisplay, actionid, action, selected);
    // reselect the item after it's been changed
    this._list.selectedIndex = selected;
    this.saveHotkeys();
  },

  _checkComboExists: function(keycombo, ignoreentry)
  {
    // checks wether the key combination already exists
    for (var i=0;i<this._list.getRowCount();i++)
    {
      if (i == ignoreentry) continue;
      var item = this._list.getItemAtIndex(i);
      if (item.keycombo == keycombo) {
        this.sbMessageBox_strings("hotkeys.hotkeyexists.title", "hotkeys.hotkeyexists.msg", "Hotkey", "This hotkey is already taken", false);
        return true;
      }
    }
    return false;
  },

  removeHotkey: function()
  {
    // remove the selected item from the list
    var index = this._list.selectedIndex;
    var item = this._list.getItemAtIndex(index);
    this._list.removeChild(item);
    // select the next item, or the previous one if that was the last
    if (index >= this._list.getRowCount()) index = this._list.getRowCount()-1;
    this._list.selectedIndex = index;
    this.saveHotkeys();
  },

  onEnableDisable: function()
  {
    this._hotkeyService.hotkeysEnabled =
                          (this._enabled.getAttribute("checked") == "true");
    this.enableDisableElements();
  },

  enableDisableElements: function() {
    var enabled = this._hotkeyService.hotkeysEnabled;
    if (enabled) {
      this._actionlist.removeAttribute("disabled");
      this._list.removeAttribute("disabled");
      this._hotkey.removeAttribute("disabled");
      this._hotkeylabel.removeAttribute("disabled");
      this._actionlabel.removeAttribute("disabled");
      this._list.setAttribute("style", "opacity: 1 !important;");
      this._hotkey.setAttribute("style", "opacity: 1 !important;");
    } else {
      this._actionlist.setAttribute("disabled", "true");
      this._list.setAttribute("disabled", "true");
      this._list.setAttribute("disabled", "true");
      this._hotkey.setAttribute("disabled", "true");
      this._hotkeylabel.setAttribute("disabled", "true");
      this._actionlabel.setAttribute("disabled", "true");
      this._list.setAttribute("style", "opacity: 0.5 !important;");
      this._hotkey.setAttribute("style", "opacity: 0.5 !important;");
    }
    this.updateButtons();
  }
};

function onHotkeysUnload()
{
  gHotkeysPane.onUnload();
}

