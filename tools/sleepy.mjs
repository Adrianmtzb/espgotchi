// Derive a "sleeping" frame from a pet sprite: every eye (connected blob of
// pixels using the species' eye palette indexes) is replaced by a closed
// lid: skin fill plus a one-pixel line in the outline color (index 0).
// Same algorithm lives in web/index.html (sleepyRows).
export function sleepyRows(rows, eyeIdx) {
  const n = rows.length;
  const g = rows.map(r => [...r].map(c => (c === "." ? -1 : +c)));
  const isEye = (r, c) => r >= 0 && c >= 0 && r < n && c < n && eyeIdx.includes(g[r][c]);
  const seen = rows.map(() => Array(n).fill(false));
  for (let r0 = 0; r0 < n; r0++) for (let c0 = 0; c0 < n; c0++) {
    if (seen[r0][c0] || !isEye(r0, c0)) continue;
    const blob = [], st = [[r0, c0]];
    seen[r0][c0] = true;
    while (st.length) {
      const [r, c] = st.pop(); blob.push([r, c]);
      for (const [dr, dc] of [[1,0],[-1,0],[0,1],[0,-1]]) {
        const rr = r + dr, cc = c + dc;
        if (isEye(rr, cc) && !seen[rr][cc]) { seen[rr][cc] = true; st.push([rr, cc]); }
      }
    }
    const rs = blob.map(b => b[0]), cs = blob.map(b => b[1]);
    const top = Math.min(...rs), bot = Math.max(...rs), left = Math.min(...cs), right = Math.max(...cs);
    if (bot === top && right - left >= 3) continue;  // a wide 1px strip is a mouth/teeth, not an eye
    // skin = most common non-eye, non-outline color touching the blob
    const cnt = {};
    for (const [r, c] of blob) for (const [dr, dc] of [[1,0],[-1,0],[0,1],[0,-1]]) {
      const rr = r + dr, cc = c + dc;
      if (rr < 0 || cc < 0 || rr >= n || cc >= n) continue;
      const v = g[rr][cc];
      if (v > 0 && !eyeIdx.includes(v)) cnt[v] = (cnt[v] || 0) + 1;
    }
    const skin = +Object.entries(cnt).sort((a, b) => b[1] - a[1])[0]?.[0] || 1;
    for (const [r, c] of blob) g[r][c] = r === bot ? 0 : skin;
  }
  return g.map(row => row.map(v => (v < 0 ? "." : String(v))).join(""));
}
