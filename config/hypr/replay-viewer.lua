-- Managed by Omarchy Replay: exclude the viewer from compositor screen copies.
-- Keep this anonymous and load it after other rules so the exclusion wins.
-- The synthetic fixture has a different app ID and remains capturable.
hl.window_rule({
  match = { initial_class = "^omarchy-replay$" },
  no_screen_share = true,
})
