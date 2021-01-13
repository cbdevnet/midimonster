-- Trackpad input by Paul Hedderly
-- Expects three sources X, Y and touch
-- On the Korg Nanopad2 these would be nanoP.ch0.cc1, nanoP.ch0.cc2, nanoP.ch0.cc16
-- so you could map and feed this script with something like:
--	[midi nanoP]
--	read = nanoPAD2
--	write = nanoPAD2
--	[lua trackpad]
--	script = trackpad.lua
-- ..	
--	nanoP.ch0.cc1 > trackpad.x
-- 	nanoP.ch0.cc2 > trackpad.y
-- 	nanoP.ch0.cc16 > trackpad.touch
--
-- Each touch will generate four outputs
-- - on[1-9] - the first point of touch (might not be very useful!)
-- - off[1-9] - the final point of touch
-- - swipe[1-9][1-9] - the first and last as a *simple* gesture or swipe
-- - gesture[1-9]..[1-9] - every segment you touch in order so you can do complicated gestures
--
-- Each output of 1 is followed by an output of 0
-- You would map these as
-- 	trackpad.on3 > ...
-- 	trackpad.off9 > ....
-- 	trackpad.swipe17 > .... -- would catch a line from top left to bottom left but could go anywhere in between
-- 	trackpad.gesture78965 > .... would catch a backwards capital L starting at the bottom left

-- -- Reserve state variables
contact=0;
trace="";
x=0; y=0
lpos=""

function x(v) -- NOTE the code assumes that we get an X before the Y - Some devices might differ!
	x=math.floor((v+0.09)*2.55)
end

function y(v)
	y=2-math.floor((v+0.09)*2.55)	-- 2- so that we have 1 at the top
	pos=""..x+1+y*3 			-- we need a string to compare
	lpos=string.sub(trace,-1)
	print("pos"..pos.." lpos"..lpos.." = "..trace)
	if pos ~= lpos then trace=trace..pos end
end

function touch(v)
	--  print("TOUCH .."..contact..".... trace"..trace)
	if v==1 then contact=1
	elseif v==0 then 
		first=string.sub(trace,1,1); last=string.sub(trace,-1)
		ends=first..last
		output("on"..last,1); output ("on"..last,0)
		output("off"..last,1); output ("off"..last,0)
		output("swipe"..ends,1); output ("swipe"..ends,0)
		output("gesture"..trace,1); output ("gesture"..trace,0)
		print("TRACKPAD>>>"..trace.." ends.."..ends)
		trace="" -- reset tracking
	end;
end
