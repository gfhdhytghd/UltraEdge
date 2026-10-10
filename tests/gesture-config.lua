local registrations, calls, keys = {}, {}, {}
hl = {
  plugin = {ultraedge = {rule = function(r) registrations[#registrations+1] = r; return true end},
    hymission = {stage_open = function() calls[#calls+1] = 'stage_open' end,
      stage_close = function() calls[#calls+1] = 'stage_close' end,
      stage_region = function(name) return {x=0,y=0,width=80,height=800} end}},
  is_key_down = function(key) return keys[key] == true end,
  dsp = {focus = function(opts) return function() calls[#calls+1] = opts.workspace end end,
    exec_cmd = function(cmd) return function() calls[#calls+1] = cmd end end},
}
dofile(arg[1])
assert(#registrations == 3)
assert(registrations[1].fingers == 3 and registrations[2].fingers == 2 and registrations[3].fingers == 4)
assert(registrations[1].on_enter() == false and #calls == 0)
keys.Super_L = true; registrations[1].on_enter(); assert(calls[1] == '+1')
assert(registrations[3].on_enter() == false and #calls == 1)
keys.Shift_L = true; registrations[3].on_enter(); assert(calls[2] == 'stage_open')
keys = {}; registrations[3].on_exit(); assert(calls[3] == 'stage_close')
registrations[2].on_enter(); assert(calls[4] == 'kitty')
print('offline gesture examples and Lua key combinations passed')
