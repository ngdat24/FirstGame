extends SceneTree
## Plays one whole game against a running server, choosing random legal actions.
##
##   godot --headless --path godot --script smoke_test.gd -- --url=http://127.0.0.1:8080
##
## Prints a PASS/FAIL line and exits with code 0 / 1.

var _failures := 0


func _initialize() -> void:
	var client: Node = preload("res://engine_client.gd").new()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--url="):
			client.base_url = arg.substr(6)
	root.add_child(client)
	_run(client)


func _check(condition: bool, what: String) -> void:
	if not condition:
		_failures += 1
		print("FAIL: ", what)


func _run(client: Node) -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 12345

	var rules: Dictionary = await client.get_rules()
	if not rules.get("ok", false):
		print("GODOT SMOKE TEST: FAIL (", rules.get("error", "?"), ": ", rules.get("message", ""), ")")
		quit(1)
		return
	_check(rules["cards"].size() == 11, "11 cards in the rules")

	var resp: Dictionary = await client.new_game(7)
	_check(resp.get("ok", false), "new_game")
	var steps := 0
	var rejected := 0
	while resp.get("ok", false) and not resp["snapshot"]["game_over"] and steps < 20000:
		var snap: Dictionary = resp["snapshot"]
		var actions: Array = snap["legal"]["actions"]
		var action: Dictionary = actions[rng.randi_range(0, actions.size() - 1)].duplicate()
		if action["type"] == "attempt_steal":
			action["amount"] = rng.randi_range(int(snap["legal"]["min_bribe"]), int(snap["legal"]["max_bribe"]))
		resp = await client.send_action(action)
		steps += 1
		if not resp.get("ok", false):
			rejected += 1
			print("  refused: ", resp)
	_check(rejected == 0, "every legal action was accepted")
	_check(resp.get("ok", false) and resp["snapshot"]["game_over"], "the game reached game over")

	if resp.get("ok", false):
		var final: Dictionary = resp["snapshot"]
		_check(int(final["winner_id"]) >= 0, "there is a winner")
		print("  game over after ", steps, " actions, winner: Player ", int(final["winner_id"]) + 1,
			", tempo ", int(final["tempo"]))

	# An illegal action must come back as ok:false, not crash anything.
	var illegal: Dictionary = await client.send_action({"type": "sell_tile", "player_id": 0, "tile_id": 3})
	_check(not illegal.get("ok", true), "an illegal action is refused")
	_check(illegal.get("error", "") == "illegal_action", "...with the error illegal_action")

	# Sending the raw floats from a parsed snapshot is the classic Godot mistake: the server refuses it.
	var raw: Dictionary = {"cmd": "action", "type": "roll_dice", "player_id": 0.0}
	var raw_resp: Dictionary = await client.call_engine(raw)
	print("  raw float player_id -> ", raw_resp.get("error", "?"))

	if _failures == 0:
		print("GODOT SMOKE TEST: PASS")
	else:
		print("GODOT SMOKE TEST: FAIL (", _failures, " problems)")
	quit(1 if _failures > 0 else 0)
