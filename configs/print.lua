-- This function prints the name of the channel it handles and it's value
-- It can be used for a simple debug output with the `default-handler` configuration option
function printchannel(value)
	print(input_channel() .. " @ " .. value)
end
