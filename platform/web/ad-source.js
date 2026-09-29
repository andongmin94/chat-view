// SPDX-License-Identifier: GPL-2.0-or-later
import { connectAd } from './ad-renderer.js';
const match = /^\/public\/ads\/([a-f0-9]{32})$/u.exec(location.pathname);
if (match) {
  /** @type {(() => void) | undefined} */
  let stop;
  let pageAvailable = true, sourceVisible = true, exiting = false;
  const update = () => {
    if (pageAvailable && sourceVisible && !exiting) stop ??= connectAd(document, match[1]);
    else { stop?.(); stop = undefined; }
  };
  // A retained OBS source is hidden without navigating or unloading its page.
  // A fresh read is required on return, even while shutdown-on-hide is off.
  // These events control local rendering only; they confer no OBS authority or
  // audience/exposure evidence. Inactive studio previews may still be visible.
  addEventListener('obsSourceVisibleChanged', event => {
    const detail = /** @type {CustomEvent<unknown>} */ (event).detail;
    sourceVisible = !!detail && typeof detail === 'object' && !Array.isArray(detail) &&
      'visible' in detail && detail.visible === true;
    update();
  });
  addEventListener('obsExit', () => { exiting = true; update(); });
  // Page and source lifetimes are independent: a late visibility event cannot
  // revive a departed document, nor can pageshow undo an OBS-hidden source.
  addEventListener('pageshow', () => { pageAvailable = true; update(); });
  addEventListener('pagehide', () => { pageAvailable = false; update(); });
  update();
}
