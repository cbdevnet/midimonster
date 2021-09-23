-- ReturnOne by Paul Hedderly
-- Sometimes you just want just an on - and from any imput but 0
-- For example I want to activate scenes in OBS from a Korg NanoPad2
-- But I dont want to have to thump the pads to get a 1.0 output
--
-- You could use this as:
--      [midi nanoP]
--      read = nanoPAD2
--      write = nanoPAD2
--      [lua trackpad]
--      script = trackpad.lua
--      default-handler = returnone
-- ..   
-- 	nanoP.ch0.note{36..51} > returnone.one{1..16} -- To feed all the 16 pads to 
--	returnone.outone1 > obs./obs/scene/1/preview
--	returnone.outone2 > obs./obs/scene/2/preview
-- etc
-- The output channel will be the same as the channel you feed prepended "out"


function returnjustone(v) -- Use a default function - then you can use any input channel name
  if v>0 then output("out"..input_channel(),1) end;
end
