extends SceneTree
## Integration test: press the real D/C/B buttons and verify server grade, cash and investment.
## Run against a --dev server: godot --headless --path godot --script grade_choice_test.gd

var main: Node
var problems: Array[String] = []


func _initialize() -> void:
	main = load("res://main.tscn").instantiate()
	root.add_child(main)
	_run()


func _check(condition: bool, message: String) -> void:
	if not condition:
		problems.append(message)


func _idle() -> void:
	var guard := 0
	while main.client._busy and guard < 600:
		await process_frame
		guard += 1
	await process_frame
	await process_frame


func _buttons(node: Node, out: Array) -> void:
	for child in node.get_children():
		if child.is_queued_for_deletion():
			continue
		if child is Button:
			out.append(child)
		_buttons(child, out)


func _run() -> void:
	await create_timer(1.0).timeout
	await _idle()
	for target in ["D", "C", "B"]:
		main._handle_response(await main.client.new_game(7))
		await _idle()
		var selected: Dictionary = {}
		for step in range(100):
			var actions: Array = main.current_snap.get("legal", {}).get("actions", [])
			for action in actions:
				if action.get("type", "") == "buy" and action.get("target_grade", "") == target:
					selected = action
					break
			if not selected.is_empty() or actions.is_empty():
				break
			main._handle_response(await main.client.send_action(actions[0]))
			await _idle()
		if selected.is_empty():
			problems.append("no purchase choice for " + target)
			continue
		var snap: Dictionary = main.current_snap
		var pid: int = int(snap["current_player_id"])
		var tid: int = int(snap["pending_tile_id"])
		var before_cash: int = int(snap["players"][pid]["cash"])
		var cost: int = int(selected["cost"])
		var expected_label: String = main._format_action_button("buy", -1, snap["tiles"], tid, selected)
		var buttons: Array = []
		_buttons(main.actions_box, buttons)
		var found: Button = null
		for button in buttons:
			if button.text == expected_label:
				found = button
		if found == null:
			problems.append("missing distinct button/price for grade " + target)
			continue
		found.pressed.emit()
		await _idle()
		var updated: Dictionary = main.current_snap
		_check(not main.status_label.text.begins_with("Error"), "purchase error for " + target)
		_check(str(updated["tiles"][tid]["grade"]) == target, "client must send target_grade " + target)
		_check(int(updated["tiles"][tid]["total_invested"]) == cost, "investment must include chosen grade steps")
		_check(int(updated["players"][pid]["cash"]) == before_cash - cost, "displayed purchase price must be charged")

	# Upgrade labels use the supplied destination and total cost, not a hardcoded one-step price.
	var tiles: Array = main.current_snap.get("tiles", []).duplicate(true)
	if tiles.size() > 3:
		tiles[3]["grade"] = "D"
		var label_c: String = main._format_action_button("upgrade", -1, tiles, 3, {"target_grade": "C", "cost": 60000})
		var label_b: String = main._format_action_button("upgrade", -1, tiles, 3, {"target_grade": "B", "cost": 120000})
		_check(label_c != label_b and label_c.contains("D → C") and label_b.contains("D → B"), "distinct upgrade destinations")
		_check(label_b.contains("120,000"), "D to B must display both upgrade steps")
	print("GRADE CHOICE TEST: ", "PASS" if problems.is_empty() else "FAIL")
	for problem in problems:
		print("  problem: ", problem)
	quit(0 if problems.is_empty() else 1)
