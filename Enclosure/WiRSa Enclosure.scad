/* [Hidden] */
point = 0.4;
wall = 4 * point;

width = 32;
height = 15;
length = 65;

deviation = 0.5;
$fn=20;

module prism(l, w, h){
    polyhedron(
        points=[[0,0,0], [l,0,0], [l,w,0], [0,w,0], [0,w,h], [l,w,h]],
        faces=[[0,1,2,3],[5,4,3,2],[0,4,5,1],[0,3,4],[5,2,1]]
    );
}

module cover(width, length, wall, deviation) {
    difference() {
        union() {
            translate([-(wall - deviation) / 2, 0, wall - deviation]) {
                cube([width + wall - deviation, length + wall + ((wall - deviation) / 2),  wall - deviation]);
            }
            cube([width, length + wall,  wall - deviation]);
            
            
        }
        //for (i=[0:10]) {
        //    translate([(width / 2) - (width * 0.8 / 2), (wall * 3) + (wall * i * 3)+5, -2]) {
        //        cube(size=[width * 0.8, wall, 10]);
        //    }
        //}
            linear_extrude(.5) {
            translate([24,48,6])
                rotate([0,0,90])
                    mirror([-1,0,0])
                        text("WiRSa", size=6, font="Courier New:style=bold");
            translate([14,63,6])
                rotate([0,0,90])
                    mirror([-1,0,0])
                        text("RetroDisks", size=7, font="Retronoid:style=Italic");
                
            }

    }
}

module enclosure(width, length, height, wall, deviation) {
    difference() {
        union() {
            translate([wall, wall, wall]) {
                cube([2, 24+8, height]);
            }
            translate([width-(wall-0.3), wall+8, wall]) {
                cube([3, 24, height]);
            }
            
            
            difference() {
                cube([width + (2 * wall), length + (2 * wall), height + wall]);
                translate([wall, wall, wall]) {
                    cube([width, length, height + 0.1]);
                }
                
                
                //Reset Hole
                translate([width, wall+4.5, wall+4.5]) {
                    rotate(a=90, [0,90,0]) 
                        cylinder(5,2.5,2.5);
                }
            }

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
        }

        //SD slots
        //translate([(width / 2)-7, 0, height-4]) {
        //    cube(size=[17, 10, 3]);
        //}

        //serial port hole
        translate([wall+1, length, 4]) {
            cube(size=[width-2, 10, height-3]);
        }
        translate([wall, length, 7.5]) {
            cube(size=[2, 5, height-10]);
        }
        
        translate([width-wall, length, 7.5]) {
            cube(size=[3, 5, height-10]);
        }
        

        
        //usb power port
        translate([wall + (width / 2) - 7, 0, wall + 2]) {
            cube([14, wall, 11]);
        }
    }
}

enclosure(width, length, height, wall, deviation);

translate([-width - 5, 0, 0]) {
    cover(width, length, wall, deviation);
}


render();