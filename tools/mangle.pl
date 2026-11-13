my ($in,$out)=@ARGV; open F,$in or die; binmode F; local $/; my $d=<F>; close F;
my %c;
for my $pair (['fragment_program','fragmentXprogram'],['fragment_shader','fragmentXshader'],['shading_language_100','shading_languageX100']) {
  my ($a,$b)=@$pair; my $p=0; while(($p=index($d,$a,$p))>=0){ my $ctx=substr($d,$p-4,4); print "  $a ctx: $ctx at $p\n"; substr($d,$p,length $a)=$b; $c{$a}++; $p++ }
}
open O,">$out" or die; binmode O; print O $d; close O; print join(",", map{"$_=$c{$_}"} keys %c),"\n";
