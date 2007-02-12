# This script is for George only, so recognize my machine :)
if [ ! -d /home/jirka/ -o ! -d /home/devgnome/ ]; then
	echo "Only George wants to run this script (it's for updating the gdm webpage)"
	exit
fi

echo rm -f *.html *.pdf
rm -f *.html *.pdf

echo docbook2html gdm.xml
docbook2html gdm.xml

echo docbook2pdf gdm.xml
docbook2pdf gdm.xml

echo scp *.html zinc.5z.com:/home/www/html/jirka/gdm-documentation/
scp *.html zinc.5z.com:/home/www/html/jirka/gdm-documentation/

echo scp gdm.pdf zinc.5z.com:/home/www/html/jirka/gdm-reference.pdf
scp gdm.pdf zinc.5z.com:/home/www/html/jirka/gdm-reference.pdf
