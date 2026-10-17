// OpenSCAD library backing files exported by `mbcad export --scad`.
// All modules are authored in Mud Buddies game space (Y up); exported files wrap the
// scene in rotate([90,0,0]) so it appears Z-up in OpenSCAD/FreeCAD.
module mb_cyl(r, r2, h, seg) { rotate([-90,0,0]) cylinder(h=h, r1=r, r2=r2, center=true, $fn=seg); }
module mb_lathe(pts, seg) { rotate([-90,0,0]) rotate_extrude($fn=seg) polygon([for (p=pts) [max(p[0],0), -p[1]]]); }
module mb_extrude(pts, depth) { rotate([0,90,0]) linear_extrude(height=depth, center=true) polygon([for (p=pts) [-p[0], p[1]]]); }
module mb_wedge(s) { mb_extrude([[-s[2]/2,-s[1]/2],[s[2]/2,-s[1]/2],[-s[2]/2,s[1]/2]], s[0]); }
module mb_tire(r, w, rim, lugs, depth, tread) {
    rt = r - depth;
    rotate([0,0,-90]) {
        mb_lathe([[rim,-w*0.41],[rim+(rt-rim)*0.5,-w*0.51],[rt,-w*0.45],[rt,w*0.45],[rim+(rt-rim)*0.5,w*0.51],[rim,w*0.41]], 48);
        for (i=[0:lugs-1]) rotate([0, -i*360/lugs, 0]) {
            if (tread == 2) translate([rt+depth*1.2,0,0]) cube([depth*2.4, w*0.96, 0.02], center=true);
            else if (tread == 1) {
                translate([rt+depth/2, w*0.25, 0]) rotate([15,0,0]) cube([depth, w*0.46, 2*PI*rt/lugs*0.48], center=true);
                rotate([0,-180/lugs,0]) translate([rt+depth/2, -w*0.25, 0]) rotate([-15,0,0]) cube([depth, w*0.46, 2*PI*rt/lugs*0.48], center=true);
            } else for (k=[-1:1]) translate([rt+depth*0.35, k*w*0.32, 0]) cube([depth*0.7, w*0.28, 2*PI*rt/lugs*0.6], center=true);
        }
    }
}
module mb_rim(r, w, spokes) {
    rotate([0,0,-90]) {
        difference() { mb_cyl(r, r, w, 32); mb_cyl(r*0.9, r*0.9, w*1.1, 32); }
        translate([0,w*0.32,0]) mb_cyl(r*0.92, r*0.92, 0.012, 32);
        translate([0,w*0.36,0]) mb_cyl(r*0.28, r*0.24, 0.05, 16);
    }
}
