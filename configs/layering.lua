-- This global variable has the current base offset for the input channels.
-- We want to map 16 input channels (from MIDI) to 512 output channels (ArtNet),
-- so we have 32 possible offsets (32 * 16 = 512)
current_layer = 0

function handler(value)
        if(input_channel() == "control") then
                -- Set the current_layer based on the control input channel
                current_layer = math.floor(value * 31.99);
        else
                -- Handler functions for the input channels
                -- Calculate the channel offset and just output the value the input channel provides
                output("out"..((current_layer * 16) + tonumber(input_channel())), value)
                print("Output on out"..(current_layer * 16))
        end
end