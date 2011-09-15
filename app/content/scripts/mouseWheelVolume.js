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

//
//  Mouse Wheel Support
//

try 
{

  // Module specific global for auto-init/deinit support
  var mousewheel_volume = {};
  mousewheel_volume.init_once = 0;
  mousewheel_volume.deinit_once = 0;
  mousewheel_volume.onLoad = function()
  {
    if (mousewheel_volume.init_once++) { dump("WARNING: mousewheel_volume double init!!\n"); return; }
    window.addEventListener("DOMMouseScroll", mousewheel_volume.onDOMMouseScroll, false);
  }
  mousewheel_volume.onUnload = function()
  {
    if (mousewheel_volume.deinit_once++) { dump("WARNING: mousewheel_volume double deinit!!\n"); return; }
    window.removeEventListener("DOMMouseScroll", mousewheel_volume.onDOMMouseScroll, false);
    window.removeEventListener("load", mousewheel_volume.onLoad, false);
    window.removeEventListener("unload", mousewheel_volume.onUnload, false);
  }

  // Auto-init/deinit registration
  window.addEventListener("load", mousewheel_volume.onLoad, false);
  window.addEventListener("unload", mousewheel_volume.onUnload, false);

  // Functionality
  mousewheel_volume.onDOMMouseScroll = function(evt)
  {
    try
    {
      var node = evt.originalTarget;
      while (node != document && node != null)
      {
        // if your object implements an event on the wheel,
        // but is not one of these, you should either give
        // it an attribute of wheelvolume="false" or
        // prevent the event from bubbling altogether
        if (node.tagName == "tree") return;
        if (node.tagName == "xul:tree") return;
        if (node.tagName == "listbox") return;
        if (node.tagName == "xul:listbox") return;
        if (node.tagName == "browser") return;
        if (node.tagName == "xul:browser") return;
        
        if (node.getAttribute && 
            node.getAttribute("wheelvolume") == "false") 
          return;
        node = node.parentNode;
      }

      if (node == null)
      {
        // could not walk up to the window before hitting a document, 
        // we're inside a sub document. the evt will continue bubbling, 
        // and we'll catch it on the next pass
        return;
      }
    
      var mm = 
        Components.classes["@songbirdnest.com/Songbird/Mediacore/Manager;1"]
                  .getService(Components.interfaces.sbIMediacoreManager);
      
      // walked up to the window
      var curVol = mm.volumeControl.volume;
      var v = curVol + ((evt.detail > 0) ? -0.03 : 0.03);
      if (v < 0) v = 0;
      if (v > 1) v = 1;
      mm.volumeControl.volume = v;
      if (v != 0) SBDataSetStringValue("faceplate.volume.last", v);
    }
    catch (err)
    {
      dump("onMouseWheelVolume - " + err);
    }
  }
}
catch (err)
{
  dump("mouseWheelVolume.js - " + err);
}


