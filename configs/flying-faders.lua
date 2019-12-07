step = 0

function wave()
	for chan=1,24 do
		output("wave" .. chan, (math.sin(math.rad((step + chan * 360 / 24) % 360)) + 1) / 2)
	end
	step = (step + 5) % 360
end

interval(wave, 100)
