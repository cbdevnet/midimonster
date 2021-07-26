--Simple lua OS examples by Paul Hedderly
-- PREREQUISITES:
-- 	luarocks install luasocket # optional if you will use your own connectors (see below)
-- 	luarocks install luabitop  # you don't need this for lua 5.3
-- 	luarocks install luasec    # you don't need this if you don't want to use SSL connections
-- 	luarocks install luamqtt

-- Example - A lot of these will depend on other configs and will need to be localised
-- 	[lua i3msg]
--	script = confgs/i3msg.lua
--	default-handler = i3msg
--
--		nanomap.{1..10} > i3msg.workspace.{1..10} -- Jump to i3 workspace
--		nanomap.10 > i3msg.muteall
--		nanomap.11 > i3msg.run.pasink
--		nanomap.12 > i3msg.showhide.retext
--		nanomap.13 > i3msg.showhide.paprefs
--		nanomap.14 > i3msg.showhide.pavucontrol
--		nanomap.15 > i3msg.showhide.keepassxc
--		nanomap.16 > i3msg.showhide.microsoft teams - insiders

--		trackpad.gestureoo852 > i3msg.macro.volup -- see my trackpad.lua example
--		trackpad.gestureoo25> i3msg.macro.voldown

function i3msg(v) -- Use a default function - then you can use any input channel name

    m=0.31
    M=0.99
    A = - M*m^2 / (M^2 - 2*m*M)
    B = M*m^2 / (M^2 - 2*m*M)
    C = math.log((M - m)^2 / m^2)
    inchan=input_channel()
    first=inchan:gsub("%..*","")
    last=inchan:gsub('^.-%.',"")

    if (v==0) then return; end -- Here were do not care that the control was released

    if (inchan == "macro.myadminpass") then
	    os.execute("/home/user/bin/macroscripttotypemyadminpassword")

    elseif (first == "workspace") then
	    -- print("i3-msg workspace number "..last)
	    os.execute("i3-msg workspace number "..last)

    elseif (first=="showhide") then
	    os.execute("i3-msg '[instance=\""..last.."\"] scratchpad show;'")

    elseif (inchan=="macro.volup") then
	    os.execute("pactl set-sink-volume 0 +5%")

    elseif (inchan=="macro.voldown") then
	    os.execute("pactl set-sink-volume 0 -10%")

    elseif (first=="sinkvol") then
	    os.execute("pactl set-sink-volume "..last.." "..math.floor(100*(A+B*math.exp(C*v))).."%")

    elseif (first=="vol") then
	    os.execute("pactl set-sink-volume "..last.." "..v)
	    
    elseif (first=="srcvol") then
	    os.execute("pactl set-source-volume "..last.." "..math.floor(100*(A+B*math.exp(C*v))).."%")

    elseif (first=="dsink") then
	    os.execute("pactl set-default-sink "..last)

    elseif (first=="dsrc") then
	    os.execute("pactl set-default-source "..last)

    elseif (first=="muteall") then
	    os.execute("~/bin/muteall")

    else
	    print("i3msg-notfound:"..first.." L:"..last.." v:"..v)
    end
end
