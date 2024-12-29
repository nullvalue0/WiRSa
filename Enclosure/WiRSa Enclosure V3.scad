render_base = true;
render_cover = true;
render_battery = true;
render_switch = true;

wall = 2.0;
width = 36.8; //37.6;
height = 6; //16;
length = 80.5; //82.8;

deviation = 0.5;
//$fn=20;
//$fs = 0.15;
$fn=60;
$fs = 0.01;

module cover(width, length, height, wall, deviation) {    
    difference() {
        union() {
            difference() {
                //cube([width + (2 * wall), length + (2 * wall), height + wall]);
                roundedcube([width + (2 * wall), length + (2 * wall), height + wall], false, .8, "zmin");
                translate([wall, wall, wall]) {
                    cube([width, length, height + 0.1]);
                }                
            }
            //snap fit bottom     
            translate([0, 0, height+wall]) {
                difference() {
                    //cube([width+(2*wall), length+(2*wall), 2]);
                    roundedcube([width+(2*wall), length+(2*wall), 2], false, .8, "z");
                    translate([1, 1, 0])
                        cube([width+(wall), length+(wall), 2]);
                }
            }            

            //display shroud
            shr_ht = 4.7;
            translate([13, 11, 1]) {
                cube([1,27,shr_ht]);
                cube([16,1,shr_ht]);
            }
            translate([28, 11, 1])
                cube([1,27,shr_ht]);
            translate([13, 37, 1])
                cube([16,1,shr_ht]);

        }
        
        //display opening
        translate([14, 12, 0]) {
            //cube([14,25,2]);
            roundedcube([14,25,2], false, 1.5, "z");
        }
        
        //power switch hole
        translate([0, wall+43.5, wall+7.5]) {
            cube([5,8,5]);
        }
              
        //SW1 Button Notch
        translate([0, wall+4, wall+5.5]) {
            cube([6,6,6],2.5,2.5);
        }
        //SW2 Button Notch
        translate([0, wall+14, wall+5.5]) {
            cube([6,6,6],2.5,2.5);
        }
        //SW3 Button Notch
        translate([0, wall+24, wall+5.5]) {
            cube([6,6,6],2.5,2.5);
        }
        //SW4 Button Notch
        translate([0, wall+34, wall+5.5]) {
            cube([6,6,6],2.5,2.5);
        }

        //serial port hole
        translate([wall+2.75, length, 2.5]) {
            cube(size=[32, 10, 10]);
        }
        
        //usb port
        translate([width+(wall*2)-14-8.8, 0, wall + 1.5])
            cube([14, 4, 8]);

        linear_extrude(.5) {
            translate([39,76,8]) //Retro
                rotate([0,0,0])
                    mirror([-1,0,0])
                        text("Retro", size=8, font="Retronoid:style=Italic");
            translate([35,67,8]) //Disks
                rotate([0,0,0])
                    mirror([-1,0,0])
                        text("D", size=8, font="Retronoid:style=Italic");
            translate([28,67,8]) //Disks
                rotate([0,0,0])
                    mirror([-1,0,0])
                        text("isks", size=8, font="Retronoid:style=Italic");
            
            translate([39.4,67,6]) //WiRSa
                rotate([0,0,0])
                    mirror([-1,0,0])
                        text("_________", size=5, font="Courier New:style=bold");


                translate([35,57,6]) //WiRSa
                    mirror([-1,0,0])
                        text("WiRSa", size=7, font="Courier New:style=bold");
            

                translate([11,wall+5,6]) //up
                        mirror([-1,0,0])
                            text("8", size=8, font="Webdings:style=bold");

                translate([11,wall+15,6]) //select
                        mirror([-1,0,0])
                            text("=", size=7.5, font="Webdings:style=bold");

                translate([11,wall+25,6])  //back
                        mirror([-1,0,0])
                            text("5", size=7.5, font="Webdings:style=bold");
                            
                translate([11,wall+35,6]) //down
                        mirror([-1,0,0])
                            text("7", size=8, font="Webdings:style=bold");
        }
        
    }
}


module base(width, length, height, wall, deviation) {
    difference() {
        union() {
            difference() {
                //cube([width + (2 * wall), length + (2 * wall), height + wall]);
                roundedcube([width + (2 * wall), length + (2 * wall), height + wall], false, .8, "zmin");
                translate([wall, wall, wall]) {
                    cube([width, length, height + 0.1]);
                }
                if (render_battery) {
                    cube([1,length+(wall*2),2]);
                    cube([width+(wall*2),1,2]);
                    translate([width+wall+1, 0, 0])
                        cube([1,length+(wall*2),2]);
                    translate([0,length+wall+1, 0])
                        cube([width+(wall*2),1,2]);
                    
                }
            }
            
            //snap fit top
            translate([1, 1, height+wall]) {
                difference() {
                    cube([width+wall, length+wall, 2]);
                translate([1, 1, 0])
                    cube([width, length, 2]);
                }
            }
        }
        
        if (render_battery) {
            //plug opening
            translate([width-6, 59, 0]) {
                cube([8,12,4]);
            } 
        }
        
        //sd card hole
        translate([0, wall+55, wall+2.5]) {
            cube([4,14,3.5]);
        }
        
        //power switch hole
        translate([width, wall+43.5, wall+3.5]) {
            cube([5,8,5]);
        }
        
        //SW1 Button Notch
        translate([width, wall+4, wall+4.5]) {
            cube([6,6,6],2.5,2.5);
        }
        //SW2 Button Notch
        translate([width, wall+14, wall+4.5]) {
            cube([6,6,6],2.5,2.5);
        }
        //SW3 Button Notch
        translate([width, wall+24, wall+4.5]) {
            cube([6,6,6],2.5,2.5);
        }
        //SW4 Button Notch
        translate([width, wall+34, wall+4.5]) {
            cube([6,6,6],2.5,2.5);
        }

        //serial port hole
        translate([wall+2.75, length, 3]) {
            cube(size=[32, 10, 10]);
        }
    }
}


module battery(width, length, height, wall, deviation) {
    
    difference() {
        union() {
            difference() {
                //cube([width + (2 * wall), length + (2 * wall), height + wall]);
                roundedcube([width + (2 * wall), length + (2 * wall), height + wall], false, .8, "zmin");
                translate([wall, wall, wall]) {
                    cube([width, length, height + 0.1]);
                }
            }
            //snap fit bottom     
            translate([0, 0, height+wall]) {
                difference() {
                    //cube([width+(2*wall), length+(2*wall), 2]);
                    roundedcube([width+(2*wall), length+(2*wall), 2], false, .8, "z");
                    translate([1, 1, 0])
                        cube([width+(wall), length+(wall), 2]);
                }
            }
        }
    }
}


module switch_cover() {
    difference() {
        cube([5.5,13,4]);
        
        translate([0,5.5,0.375])
            cube([3,2.25,1.75]);
        translate([1,0,0])
            cube([5,4.5,5]);
        translate([1,8.5,0])
            cube([5,5,5]);
    }
}

rot = 0;

if (render_base)
    base(width, length, height, wall, deviation);

if (render_cover)
    translate([-width-5,0,0])
        cover(width, length, height+1.5, wall, deviation);

if (render_battery)
    translate([width+5, length+5, 0])
        rotate([0,0,90])
            battery(width, length, height+3.75, wall, deviation);

if (render_switch)
    translate([-20, 19, 0])
        rotate([0,270,0])
            switch_cover();

module roundedcube(size = [1, 1, 1], center = false, radius = 0.5, apply_to = "all") {
	// If single value, convert to [x, y, z] vector
	size = (size[0] == undef) ? [size, size, size] : size;

	translate_min = radius;
	translate_xmax = size[0] - radius;
	translate_ymax = size[1] - radius;
	translate_zmax = size[2] - radius;

	diameter = radius * 2;

	obj_translate = (center == false) ?
		[0, 0, 0] : [
			-(size[0] / 2),
			-(size[1] / 2),
			-(size[2] / 2)
		];

	translate(v = obj_translate) {
		hull() {
			for (translate_x = [translate_min, translate_xmax]) {
				x_at = (translate_x == translate_min) ? "min" : "max";
				for (translate_y = [translate_min, translate_ymax]) {
					y_at = (translate_y == translate_min) ? "min" : "max";
					for (translate_z = [translate_min, translate_zmax]) {
						z_at = (translate_z == translate_min) ? "min" : "max";

						translate(v = [translate_x, translate_y, translate_z])
						if (
							(apply_to == "all") ||
							(apply_to == "xmin" && x_at == "min") || (apply_to == "xmax" && x_at == "max") ||
							(apply_to == "ymin" && y_at == "min") || (apply_to == "ymax" && y_at == "max") ||
							(apply_to == "zmin" && z_at == "min") || (apply_to == "zmax" && z_at == "max")
						) {
							sphere(r = radius);
						} else {
							rotate = 
								(apply_to == "xmin" || apply_to == "xmax" || apply_to == "x") ? [0, 90, 0] : (
								(apply_to == "ymin" || apply_to == "ymax" || apply_to == "y") ? [90, 90, 0] :
								[0, 0, 0]
							);
							rotate(a = rotate)
							cylinder(h = diameter, r = radius, center = true);
						}
					}
				}
			}
		}
	}
}

render();