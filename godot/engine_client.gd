extends Node
## Talks to the local HUST Monopoly server (see PROTOCOL.md in the C++ folder).
##
## Every call returns the server's JSON answer as a Dictionary:
##   {"ok": true, "snapshot": {...}, "events": [...]}   or   {"ok": false, "error": ..., "message": ...}
## Problems reaching the server come back the same way, with error "no_server" or "http_error",
## so the UI only ever has to look at "ok".
##
## Usage (add this script as a child node, or as an Autoload; the node must be in the scene tree):
##   var resp: Dictionary = await client.new_game()
##   var snap: Dictionary = resp["snapshot"]
##   resp = await client.send_action(snap["legal"]["actions"][0])
##
## Await each call before making the next one: there is one HTTPRequest, so one request at a time.

const DEFAULT_URL := "http://127.0.0.1:8080"

var base_url: String = DEFAULT_URL
var _http: HTTPRequest
var _busy := false


## Created on first use (not in _ready), so the client works even if it is called before
## Godot has run its _ready, e.g. from a SceneTree script or right after add_child.
func _ensure_http() -> void:
	if _http == null:
		_http = HTTPRequest.new()
		_http.timeout = 10.0
		add_child(_http)


## Sends one raw protocol request (a Dictionary) and returns the parsed answer.
func call_engine(request: Dictionary) -> Dictionary:
	if _busy:
		return _error("busy", "a request is already in flight; await the previous call first")
	if not is_inside_tree():
		return _error("not_in_tree", "add the client node to the scene tree (and wait one frame) before using it")
	_ensure_http()
	_busy = true
	var headers := PackedStringArray(["Content-Type: application/json"])
	var err := _http.request(base_url + "/api", headers, HTTPClient.METHOD_POST, JSON.stringify(request))
	if err != OK:
		_busy = false
		return _error("no_server", "could not start the request (error %d)" % err)
	var result: Array = await _http.request_completed
	_busy = false
	if int(result[0]) != HTTPRequest.RESULT_SUCCESS:
		return _error("no_server", "cannot reach %s (result %d)" % [base_url, int(result[0])])
	var body: PackedByteArray = result[3]
	var parsed: Variant = JSON.parse_string(body.get_string_from_utf8())
	if typeof(parsed) != TYPE_DICTIONARY:
		return _error("http_error", "HTTP %d with a body that is not JSON" % int(result[1]))
	return parsed


func get_rules() -> Dictionary:
	return await call_engine({"cmd": "get_rules"})


## game_seed is only honoured by a server started with --dev.
func new_game(game_seed: int = -1) -> Dictionary:
	var request := {"cmd": "new_game"}
	if game_seed >= 0:
		request["seed"] = game_seed
	return await call_engine(request)


func get_state() -> Dictionary:
	return await call_engine({"cmd": "get_state"})


## `action` is one entry of snapshot["legal"]["actions"] (for "attempt_steal" set "amount" to the
## bribe first). Godot's JSON parser turns every number into a float, and the server refuses
## floats such as 3.0, so the fields are converted back to integers here.
func send_action(action: Dictionary) -> Dictionary:
	return await call_engine({
		"cmd": "action",
		"type": str(action.get("type", "")),
		"player_id": int(action.get("player_id", 0)),
		"tile_id": int(action.get("tile_id", -1)),
		"amount": int(action.get("amount", 0)),
	})


func _error(code: String, message: String) -> Dictionary:
	return {"ok": false, "error": code, "message": message}
