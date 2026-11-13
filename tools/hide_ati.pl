my ($in,$out)=@ARGV;
open F,$in or die; binmode F; local $/; my $d=<F>; close F;
my $n=0; my $p=0;
while(($p=index($d,"GL_ATI_text_fragment_shader",$p))>=0){ substr($d,$p,1)="X"; $n++; $p++ }
open O,">$out" or die; binmode O; print O $d; close O;
print "patched $n\n";
