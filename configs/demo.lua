-- This example MIDIMonster Lua script spreads one input channel onto multiple output
-- channels using a polynomial function evaluated at multiple points. This effect can
-- be visualized e.g. with martrix (https://github.com/cbdevnet/martrix).

-- The polynomial to evaluate
function polynomial(x)
	return math.exp(-40 * input_value("width") * (x - input_value("offset")) ^ 2)
end

-- Evaluate and set output channels
function evaluate()
	for chan=0,10 do
		output("out" .. chan, polynomial((1 / 10) * chan))
	end
end

-- Handler functions for the input channels
function offset(value)
	evaluate()
end

function width(value)
	evaluate()
end

-- This is an example showing a simple chase running on its own without the need
-- (but the possibility) for external input

-- Global value for the current step
current_step = 0

function step()
	if(current_step > 0) then
		output("dim", 0.0)
	else
		output("dim", 1.0)
	end
		
	current_step = (current_step + 1) % 2
end

interval(step, 1000)
