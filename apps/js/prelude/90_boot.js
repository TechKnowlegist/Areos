// 90_boot.js - what the host calls. Returns the object jshost.c keeps.

K.nextTimer = function () {
  let wait = -1;
  const t = now();
  for (const x of timerList) { const w = Math.max(0, x.due - t); if (wait < 0 || w < wait) wait = w; }
  if (rafList.length) { const w = Math.max(0, nextFrame - t); if (wait < 0 || w < wait) wait = w; }
  return Math.ceil(wait);
};

return {
  start: K.start,
  step: K.step,
  loading: K.loading,
  timers: K.runTimers,
  nextTimer: K.nextTimer,
  render: K.render,
  renderFull: K.renderFull,
  click: K.userClick,
  input: K.userInput,
  key: K.userKey,
  resolveModule: K.resolveModule,
  canGo: (d) => { const h = K.history; const to = h._i + d; return to >= 0 && to < h._e.length; },
  go: (d) => { K.history.go(d); return 1; },
  consoleText: K.consoleText,
};
