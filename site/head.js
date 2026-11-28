// SPDX-License-Identifier: GPL-3.0-or-later
// Before the first paint (loaded plainly, not deferred, from <head>): the hero waits for its entrance
// (assets/motion), and the moments (assets/moments) know scripts run. The two timers show everything if a
// module is late or fails, so the page never stays hidden.
document.documentElement.classList.add("m-enter", "mo-js");
setTimeout(() => document.documentElement.classList.add("m-enter-done"), 700);
setTimeout(() => document.documentElement.classList.add("mo-safe"), 2500);
