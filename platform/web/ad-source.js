// SPDX-License-Identifier: GPL-2.0-or-later
import { connectAd } from './ad-renderer.js';
const match = /^\/public\/ads\/([a-f0-9]{32})$/u.exec(location.pathname);
if (match) {
  /** @type {(() => void) | undefined} */
  let stop;
  const start = () => { stop ??= connectAd(document, match[1]); };
  // A restored document needs a new read; never reuse its old artwork/deadline.
  // Repeated pageshow events must not create concurrent polling loops.
  addEventListener('pageshow', start);
  addEventListener('pagehide', () => { stop?.(); stop = undefined; });
  start();
}
