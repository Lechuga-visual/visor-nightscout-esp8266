from pathlib import Path
from html import escape
import argparse
try:
    from tools.localization import translate
except ModuleNotFoundError:
    from localization import translate
parser=argparse.ArgumentParser(description="Draw the documented seven-wire pin mapping as an SVG.")
parser.add_argument('--language', choices=['es','en'], default='en')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
suffix='-en' if args.language=='en' else ''
out=root / ('docs/images/conexiones-wemos-gc9a01'+suffix+'.svg')
s=[]
s.append('''<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="1760" viewBox="0 0 1600 1760" role="img" aria-labelledby="title desc">
<title id="title">Conectar un Wemos D1 mini ESP8266 a una pantalla GC9A01 SPI de siete pines</title>
<desc id="desc">Con la alimentación desconectada: pantalla VCC a Wemos 3V3; GND a G; SCL a D5 GPIO 14; SDA a D7 GPIO 13; DC a D1 GPIO 5; CS a D2 GPIO 4; RST a D0 GPIO 16. La vista de las placas es ilustrativa: seguir siempre sus etiquetas. SDA y SCL son SPI. No conectar VCC a 5V ni RST de la pantalla a RST del Wemos.</desc>
<style>text{font-family:DejaVu Sans,Arial,sans-serif;fill:#172639}.title{font-size:48px;font-weight:700}.h{font-size:29px;font-weight:700}.body{font-size:23px}.small{font-size:20px;fill:#4b5b71}.pin{font-size:24px;font-weight:700}.white{fill:#fff}.muted{fill:#b0c6dd}</style>
<rect width="1600" height="1760" fill="#f3f6fa"/>
''')
def text(x,y,t,cls='body',anchor=None,fill=None):
    extra=(f' text-anchor="{anchor}"' if anchor else '')+(f' style="fill:{fill}"' if fill else '')
    s.append(f'<text x="{x}" y="{y}" class="{cls}"{extra}>{escape(t)}</text>')
def rect(x,y,w,h,fill,rx=18,stroke=None):
    s.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}"'+(f' stroke="{stroke}" stroke-width="2"' if stroke else '')+'/>')
def circle(x,y,r,fill,stroke=None,sw=2):
    s.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}"'+(f' stroke="{stroke}" stroke-width="{sw}"' if stroke else '')+'/>')
def line(x1,y1,x2,y2,c,w=4):
    s.append(f'<path d="M{x1} {y1}H{x2}" stroke="{c}" stroke-width="{w}" fill="none" stroke-linecap="round"/>' if y1==y2 else f'<path d="M{x1} {y1}L{x2} {y2}" stroke="{c}" stroke-width="{w}" fill="none"/>')
text(60,80,'Wemos D1 mini + GC9A01','title')
text(60,125,'Guía de conexión · ESP8266 · pantalla SPI 240 × 240 · firmware 0.7.1','body')
rect(50,165,1500,68,'#fff0d2',14)
text(78,209,'ANTES DE CONECTAR: retira el USB y cualquier otra alimentación.','h')
# Identification cards; the board arrangement matches the original example photo.
rect(50,255,730,510,'#ffffff',22,'#dae2eb')
rect(820,255,730,510,'#ffffff',22,'#dae2eb')
text(80,301,'1  Identifica los pines del Wemos','h')
text(850,301,'2  Lee las etiquetas de la pantalla','h')
text(80,336,'Ejemplo con USB arriba; cara del ESP8266 visible.','small')
rect(290,378,260,310,'#125381',28)
rect(378,354,84,43,'#b9c7d5',6,'#61758c')
rect(391,359,58,20,'#34465c',3)
text(420,414,'D1 MINI','small','middle','#d6edf8')
rect(351,449,138,125,'#c2cbd3',8,'#8294a3')
text(420,507,'ESP8266','body','middle','#23384b')
text(420,537,'Wi-Fi','small','middle','#425b6c')
s.append('<path d="M356 642V602H380V642H405V602H430V642H455V602H481V650" fill="none" stroke="#d6b65a" stroke-width="6"/>')
left=['5V','G','D4','D3','D2','D1','RX','TX']
right=['3V3','D8','D7','D6','D5','D0','A0','RST']
colors={'3V3':'#d8344a','G':'#475569','D5':'#a0740c','D7':'#218544','D1':'#007c9b','D2':'#c76512','D0':'#a33ca0'}
for i,(l,r) in enumerate(zip(left,right)):
    y=406+i*35
    for x,label,tx,anchor in [(304,l,273,'end'),(536,r,567,'start')]:
        c=colors.get(label,'#a9bbc9')
        circle(x,y,8,'#ffffff',c,4)
        text(tx,y+8,label,'pin',anchor,c if label in colors else '#718095')
text(80,724,'Si tu placa cambia de orientación, busca cada nombre.','small')
# Display illustration; module pin order is explicitly illustrative.
rect(1000,366,370,299,'#145783',22)
for x,y in [(1018,384),(1352,384),(1018,646),(1352,646)]:circle(x,y,7,'#c3d2df')
circle(1185,505,125,'#12212c','#1f3446',5)
circle(1185,505,118,'#0c1720','#70dcbc',3)
text(1185,492,'GC9A01','h','middle','#e3f2f3')
text(1185,528,'240 × 240','body','middle','#bcd3d9')
labels=['VCC','GND','SCL','SDA','DC','CS','RST']
for i,label in enumerate(labels):
    x=1041+i*48
    circle(x,671,6,'#d3b45e','#886a2f')
    text(x,701,label,'small','middle','#172639')
text(850,337,'Módulo de siete pines; el orden puede variar.','small')
text(850,740,'Conecta por etiqueta, no por la posición del dibujo.','small')
# Cable map. Grouped terminals deliberately do not imply a physical header order.
rect(50,795,1500,720,'#ffffff',22,'#dae2eb')
text(80,847,'3  Une estos siete pares, uno a uno','h')
text(80,883,'Los terminales se agrupan aquí para seguir cada cable sin cruces.','small')
text(245,931,'WEMOS D1 MINI','pin','middle')
text(1305,931,'PANTALLA GC9A01','pin','middle')
rows=[('3V3','VCC','Alimentación · 3,3 V','#d8344a'),('G / GND','GND','Tierra común','#475569'),('D5','SCL / CLK','Reloj SPI · GPIO 14','#a0740c'),('D7','SDA / DIN / MOSI','Datos SPI · GPIO 13','#218544'),('D1','DC','Datos / instrucciones · GPIO 5','#007c9b'),('D2','CS','Selección · GPIO 4','#c76512'),('D0','RST / RES','Reinicio de pantalla · GPIO 16','#a33ca0')]
for i,(wemos,display,meaning,c) in enumerate(rows):
    y=977+i*69
    if i%2==0:rect(75,y-28,1450,56,'#f5f8fb',10)
    circle(106,y,15,c)
    text(106,y+7,str(i+1),'small','middle','#ffffff')
    text(245,y+8,wemos,'h','middle',c)
    circle(415,y,8,'#ffffff',c,4)
    circle(1080,y,8,'#ffffff',c,4)
    line(426,y,1069,y,c,5)
    rect(485,y-18,525,36,'#ffffff',12)
    text(747,y+7,meaning,'small','middle')
    text(1305,y+8,display,'pin','middle',c)
text(80,1490,'Los colores ayudan a seguir el dibujo: el cable puede ser de otro color.','small')
# Safety and signal notes as part of diagram, without requiring surrounding Markdown.
rect(50,1545,1500,150,'#e5edf6',20)
text(80,1587,'VCC → 3V3, nunca 5V.    RST de pantalla → D0, no RST del Wemos.','pin')
text(80,1628,'SCL y SDA son SPI en esta pantalla. No se conectan como I²C. No necesita MISO.','body')
text(80,1666,'Alimenta el Wemos por USB después de revisar todo el cableado.','body')
text(60,1735,'Esquema ilustrativo · Sigue las etiquetas de tus módulos · Guía completa: docs/INSTALACION.md','small')
s.append('</svg>')
content='\n'.join(s)+'\n'
out.write_text(translate(content) if args.language=='en' else content, encoding='utf-8')
print(out.relative_to(root))
