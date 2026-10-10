-- Offline only: API fixtures, no compositor calls, shell execution or mouse input.
local config=arg[1] or 'examples/hyprv.lua'
local registrations,commands={},{}
local windows={}
hl={plugin={ultraedge={clear=function() registrations={} end,rule=function(r) registrations[#registrations+1]=r;return true end},hymission={stage_open=function() end,stage_close=function() end,stage_region=function(name)return {x=-1000,y=-200,width=120,height=800}end}},get_windows=function()return windows end,exec_cmd=function(cmd)commands[#commands+1]=cmd end}
dofile(config)
assert(#registrations==2)
assert(registrations[1].hold('L').width==120)
local mon={name='R',x=-100,y=-200,width=2560,height=1440,scale=2,transform=0}
windows={{class='ai-hub-firefox',mapped=true,hidden=false,workspace={name='1'},monitor=mon,at={x=800,y=-140}}}
local r=registrations[2].hold('R');assert(r.x==800 and r.y==-200 and r.width==380 and r.height==720)
windows[1].at.x=700;assert(registrations[2].hold('R').width==480)
assert(registrations[2].hold('L')==nil)
windows[1].workspace.name='special:sidechat_hidden';assert(registrations[2].hold('R')==nil)
windows[1].workspace.name='1';mon.transform=1;windows[1].at.x=300
r=registrations[2].hold('R');assert(r.width==320 and r.height==1280)
registrations[2].on_enter("DP-1'$bad")
assert(commands[1]=="bash /home/wilf/.config/HyprV/hypr/scripts/sidechat.sh show 'DP-1'\\''$bad'")
registrations[2].on_exit();assert(commands[2]:match(' hide$'))
hl.plugin.hymission={};dofile(config);assert(#registrations==1)
hl.plugin.ultraedge=nil;dofile(config)
print('offline Lua guards, dynamic geometry, rotation, visibility and command quoting passed')
