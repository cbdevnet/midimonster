-- Trackpad input by Paul Hedderly
-- Expects two inputs - one master and one slave
-- The first is to track a contolled input for example a
-- sound desk fader.
-- The slave is for a fader on a non-motorised surface...
-- The idea is that the slave is not forwarded to the
-- first/main output until it is close in value to the master
-- This is to avoid sudden jumps when using a secondary
-- non-motorised controller.
--
-- Example config - here using a nanok as a slave to an X32
--
--	[midi x32]
--	read = X32Live
--	write = X32Live
--
--	[midi nanoK]
--	read = nanoKEY
--	write = nanoKEY
--
--	[lua latch]
--	script = latch.lua
--	default-handler = latch
--
--	x32.ch0.cc1 > latch.0.master
--	nanoK.ch0.cc1 > latch.0.slave
--	latch.0.latched > x32.ch0.cc1

threshold=0.03
saved={}

function latch(v)
	i=input_channel(); separator=i:find("%.")
	channel=i:sub(0,separator-1)
	control=i:sub(1+separator,-1)

	-- Setup the saved value if not yet seen
	if saved[channel]==nil then
		saved[channel]=v
	end

	if control == "master" then
		saved[channel]=v
	elseif control == "slave" then
		diff=math.abs(v-saved[channel])
		if diff < threshold then
			saved[channel]=v
			output(channel..".latched",v)
		end
	end
end
