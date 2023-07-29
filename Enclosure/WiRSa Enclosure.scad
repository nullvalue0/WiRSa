/* [Hidden] */
//wall = 1.6;
wall = 2.0;

/*width = 31.8;
height = 16.1;
length = 65.3;*/

width = 32.8;
height = 16.4;
length = 66.3;


deviation = 0.5;
cover_shrinkage = 0.2;
$fn=20;

module cover(width, length, wall, deviation) {
    difference() {
        union() {
            translate([-(wall - deviation) / 2, 0, wall - deviation]) {
                cube([width + wall - deviation-cover_shrinkage, length + wall + ((wall - deviation) / 2),  wall - deviation]);
            }
            cube([width-cover_shrinkage, length + wall,  wall - deviation]);
        }
        
        //display opening
        translate([4, 14.5, 0]) {
            cube([24,14,4]);
        }

        linear_extrude(.5) {
            translate([29.5,60,8]) //Retro
                rotate([0,0,0])
                    mirror([-1,0,0])
                        text("Retro", size=6.5, font="Retronoid:style=Italic");
            translate([29.5,53,8]) //Disks
                rotate([0,0,0])
                    mirror([-1,0,0])
                        text("D", size=6.5, font="Retronoid:style=Italic");
            translate([23.5,53,8]) //Disks
                rotate([0,0,0])
                    mirror([-1,0,0])
                        text("isks", size=6.5, font="Retronoid:style=Italic");
            
            translate([30,5,6]) //WiRSa
                rotate([0,0,0])
                    mirror([-1,0,0])
                        text("WiRSa", size=7, font="Courier New:style=bold");

            translate([32,45,6]) //select
                    mirror([-1,0,0])
                        text("=", size=7.5, font="Webdings:style=bold");

            translate([11,45,6]) //up
                    mirror([-1,0,0])
                        text("5", size=8, font="Webdings:style=bold");

            translate([32,36,6])  //back
                    mirror([-1,0,0])
                        text("7", size=7.5, font="Webdings:style=bold");
                        
            translate([11,36,6]) //down
                    mirror([-1,0,0])
                        text("6", size=8, font="Webdings:style=bold");
        }
    }
}

module enclosure(width, length, height, wall, deviation) {
    difference() {
        union() {                      
            //top rails
            translate([0, 0, wall + height]) {
                cube([wall / 2, (length + wall * 2), wall]);
            }
            translate([0, 0, height + (wall * 2)]) {
                cube([wall, (length + wall * 2), wall - deviation]);
            }

            translate([width + wall + (wall / 2), 0, wall + height]) {
                cube([wall / 2, (length + wall * 2), wall]);
            }
            translate([width + wall, 0, height + (wall * 2)]) {
                cube([wall, (length + wall * 2), wall - deviation]);
            }

            translate([0, length + wall + (wall / 2), wall + height]) {
                cube([(width + wall * 2), wall / 2, wall]);
            }
            translate([0, length + wall, height + (wall * 2)]) {
                cube([(width + wall * 2), wall, wall - deviation]);
            }
            
            difference() {
                cube([width + (2 * wall), length + (2 * wall), height + wall]);
                translate([wall, wall, wall]) {
                    cube([width, length, height + 0.1]);
                }
                
            }
        }

        //Reset Hole
        translate([width, wall+4.5, wall+4.5]) {
            rotate(a=90, [0,90,0]) 
                cylinder(5,2.5,2.5);
        }
        
        //SW1 Button Notch
        translate([width, wall+45, wall+10]) {
            cube([4,4,height],2.5,2.5);
        }
        //SW2 Button Notch
        translate([width, wall+35, wall+10]) {
            cube([4,4,height],2.5,2.5);
        }
        //SW3 Button Notch
        translate([-1, wall+45, wall+10]) {
            cube([4,4,height],2.5,2.5);
        }
        //SW4 Button Notch
        translate([-1, wall+35 , wall+10]) {
            cube([4,4,height],2.5,2.5);
        }

        //serial port hole
        translate([wall+1, length, 4]) {
            cube(size=[width-2, 10, height-4]);
        }
        translate([wall, length, 7.5]) {
            cube(size=[2, 5, height-11]);
        }        
        translate([width-wall+1, length, 7.5]) {
            cube(size=[3, 5, height-11]);
        }        
        
        //usb power port & sd card
        translate([wall + (width / 2) - 7, 0, wall + 2]) {
            cube([14, wall, 12]);
        }
    }
}

enclosure(width, length, height, wall, deviation);

translate([-width - 5, 0, 0]) {
    cover(width, length, wall, deviation);
}

render();