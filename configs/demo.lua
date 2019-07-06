-- This example MIDIMonstaer Lua script spreads one input channel onto multiple output
-- channels using a polynomial function evaluated at multiple points. This effect can
-- be visualized e.g. with martrix (https://github.com/cbdevnet/martrix).

-- This is just a demonstration of global variables
foo = 0

-- The polynomial to evaluate
function polynomial(offset, x)
	return math.exp(-20 * (x - offset) ^ 2)
end

-- Handler function for the input channel
function input(value)
	foo = foo + 1
	print("input at ", value, foo)

	for chan=0,10 do
		output("out" .. chan, polynomial(value, (1 / 10) * chan))
	end
end
