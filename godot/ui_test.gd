extends SceneTree
## Plays a whole game through the REAL UI (main.tscn) by pressing its real buttons, and fails if
## anything goes wrong. Needs the server running; --dev makes the game reproducible.
##
##   ./server --dev
##   godot --headless --path godot --script ui_test.gd -- --seed=7
##
## Optional: --shots=/some/folder saves PNG screenshots of key moments. That needs a real window
## (not --headless), e.g. on Linux:  xvfb-run -a godot --rendering-method gl_compatibility ...
##
## It checks that (1) the whole game can be played with the first available button each time,
## (2) the UI never shows an error banner, and (3) the bribe controls send EXACTLY the amount and
## chance that the button displays (this used to be wrong: the number box snapped to steps of 5000).

var main: Node
var seed_value := -1
var shots_dir := ""
var problems: Array[String] = []
var seen_phases := {}


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--seed="):
			seed_value = int(arg.substr(7))
		elif arg.begins_with("--shots="):
			shots_dir = arg.substr(8)
	main = load("res://main.tscn").instantiate()
	root.add_child(main)
	_run()


func _shot(shot_name: String) -> void:
	if shots_dir == "":
		return
	await process_frame
	await process_frame
	DirAccess.make_dir_recursive_absolute(shots_dir)
	root.get_texture().get_image().save_png("%s/%s.png" % [shots_dir, shot_name])


func _wait_idle() -> void:
	var guard := 0
	while main.client._busy and guard < 600:
		await process_frame
		guard += 1
	await process_frame
	await process_frame


func _descendants(n: Node, out: Array) -> void:
	for c in n.get_children():
		if c.is_queued_for_deletion():
			continue
		out.append(c)
		_descendants(c, out)


func _find_all(type_name: String) -> Array:
	var nodes: Array = []
	_descendants(main.actions_box, nodes)
	return nodes.filter(func(n: Node) -> bool: return n.get_class() == type_name)


func _test_bribe_controls() -> void:
	var slider: HSlider = _find_all("HSlider")[0]
	var spin: SpinBox = _find_all("SpinBox")[0]
	var tested := 0
	for target in [33333, 12345, 7777, 99999, int(slider.max_value), int(slider.min_value)]:
		if target < slider.min_value or target > slider.max_value:
			continue
		slider.value = target
		await process_frame
		var steal_text := ""
		for b in _find_all("Button"):
			if "%" in b.text and main._fmt(target) in b.text:
				steal_text = b.text
		if int(spin.value) != target:
			problems.append("bribe: slider %d but the number box would send %d" % [target, int(spin.value)])
		if steal_text == "":
			problems.append("bribe: the steal button does not show %s" % main._fmt(target))
		tested += 1
	if tested == 0:
		problems.append("bribe: no testable amounts in the slider range")


func _run() -> void:
	await create_timer(1.0).timeout
	if seed_value >= 0:
		main._handle_response(await main.client.new_game(seed_value))
	await _wait_idle()
	await _shot("01_start")
	var steps := 0
	var bribe_tested := false
	while steps < 3000:
		var snap: Dictionary = main.current_snap
		if snap.is_empty():
			problems.append("no game state: is the server running? (" + main.status_label.text + ")")
			break
		if snap.get("game_over", false):
			break
		var phase := str(snap.get("phase", ""))
		if not seen_phases.has(phase):
			seen_phases[phase] = true
			await _shot("phase_" + phase)
		if main.status_label.text.begins_with("Error"):
			problems.append("error banner: " + main.status_label.text)
			break
		if not bribe_tested and _find_all("HSlider").size() > 0:
			bribe_tested = true
			await _test_bribe_controls()
			await _shot("steal_panel")
		var buttons := _find_all("Button")
		if buttons.is_empty():
			problems.append("no buttons to press in phase " + phase)
			break
		(buttons[0] as Button).pressed.emit()
		await _wait_idle()
		steps += 1
	await _shot("99_end")
	var finished: bool = main.current_snap.get("game_over", false)
	if not finished:
		problems.append("the game did not reach game over")
	print("UI TEST: ", "PASS" if problems.is_empty() else "FAIL", "  steps=", steps, "  phases=",
		seen_phases.keys(), "  bribe_tested=", bribe_tested)
	for p in problems:
		print("  problem: ", p)
	quit(0 if problems.is_empty() else 1)
