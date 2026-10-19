"""Real racing drivers for make_ai_drivers_pack.py.

One line per driver: First|Last|Country code[|E]. Lists are per FM4 AI region
(1 = Asia, 2 = Europe/Oceania/Africa, 3 = Americas), roughly strongest first:
the builder spreads each list over career ranks 1 (best) to 16 in this order.
A trailing |E shows the name family-name first (e.g. "Zhou Guanyu").
Names are plain ASCII on purpose (no accents), so every game font can draw them.
"""

# code: (CountryID or None for a new row, RegionID, display name, bio name, flag file stem)
COUNTRIES = {
    'AUS': (1, 2, 'AUSTRALIA', 'Australia', 'Australia'),
    'FRA': (2, 2, 'FRANCE', 'France', 'France'),
    'GER': (3, 2, 'GERMANY', 'Germany', 'Germany'),
    'ITA': (4, 2, 'ITALY', 'Italy', 'Italy'),
    'JPN': (5, 1, 'JAPAN', 'Japan', 'Japan'),
    'KOR': (6, 1, 'KOREA', 'Korea', 'Korea'),
    'ESP': (7, 2, 'SPAIN', 'Spain', 'Spain'),
    'SWE': (8, 2, 'SWEDEN', 'Sweden', 'Sweden'),
    'GBR': (9, 2, 'UNITED KINGDOM', 'Britain', 'UnitedKingdom'),
    'USA': (10, 3, 'UNITED STATES', 'the USA', 'UnitedStates'),
    'MON': (11, 2, 'MONACO', 'Monaco', 'monaco'),
    'NED': (12, 2, 'NETHERLANDS', 'the Netherlands', 'Netherlands'),
    'POR': (13, 2, 'PORTUGAL', 'Portugal', 'portugal'),
    'RSA': (15, 2, 'SOUTH AFRICA', 'South Africa', 'southafrica'),
    'AUT': (16, 2, 'AUSTRIA', 'Austria', 'austria'),
    'BEL': (17, 2, 'BELGIUM', 'Belgium', 'belgium'),
    'DEN': (18, 2, 'DENMARK', 'Denmark', 'denmark'),
    'MEX': (20, 3, 'MEXICO', 'Mexico', 'mexico'),
    'CZE': (21, 2, 'CZECH REPUBLIC', 'the Czech Republic', 'czechrepublic'),
    'NZL': (22, 2, 'NEW ZEALAND', 'New Zealand', 'newzealand'),
    'SUI': (23, 2, 'SWITZERLAND', 'Switzerland', 'switzerland'),
    # new List_Country rows (ids given by the builder, from 30)
    'BRA': (None, 3, 'BRAZIL', 'Brazil', 'brazil'),
    'CAN': (None, 3, 'CANADA', 'Canada', 'canada'),
    'COL': (None, 3, 'COLOMBIA', 'Colombia', 'columbia'),
    'CHI': (None, 3, 'CHILE', 'Chile', 'chile'),
    'FIN': (None, 2, 'FINLAND', 'Finland', 'finland'),
    'NOR': (None, 2, 'NORWAY', 'Norway', 'norway'),
    'POL': (None, 2, 'POLAND', 'Poland', 'poland'),
    'RUS': (None, 2, 'RUSSIA', 'Russia', 'russia'),
    'IRL': (None, 2, 'IRELAND', 'Ireland', 'ireland'),
    'HUN': (None, 2, 'HUNGARY', 'Hungary', 'hungary'),
    'CHN': (None, 1, 'CHINA', 'China', 'china'),
    'HKG': (None, 1, 'HONG KONG', 'Hong Kong', 'hongkong'),
    'IND': (None, 1, 'INDIA', 'India', 'india'),
    'MAS': (None, 1, 'MALAYSIA', 'Malaysia', 'malaysia'),
    'PHI': (None, 1, 'PHILIPPINES', 'the Philippines', 'philippines'),
}

ASIA = """
Takuma|Sato|JPN
Kamui|Kobayashi|JPN
Kazuyoshi|Hoshino|JPN
Satoshi|Motoyama|JPN
Aguri|Suzuki|JPN
Satoru|Nakajima|JPN
Narain|Karthikeyan|IND
Guanyu|Zhou|CHN|E
Yuki|Tsunoda|JPN
Ukyo|Katayama|JPN
Kunimitsu|Takahashi|JPN
Masahiro|Hasemi|JPN
Keiichi|Tsuchiya|JPN
Ryo|Hirakawa|JPN
Kazuki|Nakajima|JPN
Naoki|Yamamoto|JPN
Tomoki|Nojiri|JPN
Karun|Chandhok|IND
Alex|Yoong|MAS
Ho-Pin|Tung|CHN
Toranosuke|Takagi|JPN
Shinji|Nakano|JPN
Hiroaki|Ishiura|JPN
Kazuya|Oshima|JPN
Yuji|Tachikawa|JPN
Juichi|Wakisaka|JPN
Ryo|Michigami|JPN
Masanori|Sekiya|JPN
Daisuke|Ito|JPN
Takashi|Kogure|JPN
Toshio|Suzuki|JPN
Yuji|Ide|JPN
Sakon|Yamamoto|JPN
Kohei|Hirate|JPN
Nirei|Fukuzumi|JPN
Sho|Tsuboi|JPN
Ritomo|Miyata|JPN
Ayumu|Iwasa|JPN
Kenta|Yamashita|JPN
Yuhi|Sekiguchi|JPN
Tadasuke|Makino|JPN
Qinghua|Ma|CHN|E
Congfu|Cheng|CHN|E
Yifei|Ye|CHN|E
Jehan|Daruvala|IND
Kush|Maini|IND
Arjun|Maini|IND
Armaan|Ebrahim|IND
Aditya|Patel|IND
Jazeman|Jaafar|MAS
Fairuz|Fauzy|MAS
Nabil|Jeffri|MAS
Marchy|Lee|HKG
Darryl|O'Young|HKG
Shaun|Thong|HKG
Adderly|Fong|HKG
Marlon|Stockinger|PHI
Jong-kyum|Kim|KOR
Hideki|Noda|JPN
Koudai|Tsukakoshi|JPN
Kazuki|Hiramine|JPN
Hiroki|Otsu|JPN
Sena|Sakaguchi|JPN
Ukyo|Sasahara|JPN
Masataka|Yanagida|JPN
Tsugio|Matsuda|JPN
Hironobu|Yasuda|JPN
Yuichi|Nakayama|JPN
Takeshi|Tsuchiya|JPN
Nobuteru|Taniguchi|JPN
Manabu|Orido|JPN
Hiro|Matsushita|JPN
Daigo|Saito|JPN
Masato|Kawabata|JPN
Toshi|Arai|JPN
Fumio|Nutahara|JPN
"""

EUROPE = """
Michael|Schumacher|GER
Lewis|Hamilton|GBR
Alain|Prost|FRA
Niki|Lauda|AUT
Jackie|Stewart|GBR
Jim|Clark|GBR
Sebastian|Vettel|GER
Fernando|Alonso|ESP
Max|Verstappen|NED
Jack|Brabham|AUS
Kimi|Raikkonen|FIN
Mika|Hakkinen|FIN
Nigel|Mansell|GBR
Stirling|Moss|GBR
Alberto|Ascari|ITA
Jochen|Rindt|AUT
Graham|Hill|GBR
Tom|Kristensen|DEN
Sebastien|Loeb|FRA
Jacky|Ickx|BEL
Damon|Hill|GBR
Jenson|Button|GBR
Nico|Rosberg|GER
Keke|Rosberg|FIN
Alan|Jones|AUS
Denny|Hulme|NZL
Bruce|McLaren|NZL
Jody|Scheckter|RSA
John|Surtees|GBR
James|Hunt|GBR
Walter|Rohrl|GER
Sebastien|Ogier|FRA
Colin|McRae|GBR
Carlos|Sainz Sr.|ESP
Charles|Leclerc|MON
Lando|Norris|GBR
Mark|Webber|AUS
Daniel|Ricciardo|AUS
Oscar|Piastri|AUS
George|Russell|GBR
Scott|Dixon|NZL
Carlos|Sainz|ESP
Valtteri|Bottas|FIN
Gerhard|Berger|AUT
Riccardo|Patrese|ITA
Ronnie|Peterson|SWE
Peter|Brock|AUS
Juha|Kankkunen|FIN
Tommi|Makinen|FIN
Marcus|Gronholm|FIN
Petter|Solberg|NOR
Robert|Kubica|POL
Clay|Regazzoni|SUI
Michele|Alboreto|ITA
Jean|Alesi|FRA
David|Coulthard|GBR
Nino|Farina|ITA
Mike|Hawthorn|GBR
Jo|Siffert|SUI
Rene|Arnoux|FRA
Didier|Pironi|FRA
Jacques|Laffite|FRA
Ari|Vatanen|FIN
Hannu|Mikkola|FIN
Kalle|Rovanpera|FIN
Craig|Lowndes|AUS
Jamie|Whincup|AUS
Mark|Skaife|AUS
Shane|van Gisbergen|NZL
Dario|Franchitti|GBR
Allan|McNish|GBR
Derek|Bell|GBR
Hans-Joachim|Stuck|GER
Bernd|Schneider|GER
Klaus|Ludwig|GER
Andre|Lotterer|GER
Benoit|Treluyer|FRA
Marcel|Fassler|SUI
Sebastien|Buemi|SUI
Brendon|Hartley|NZL
Earl|Bamber|NZL
Chris|Amon|NZL
Pierre|Gasly|FRA
Esteban|Ocon|FRA
Nico|Hulkenberg|GER
Kevin|Magnussen|DEN
Stefan|Bellof|GER
Jochen|Mass|GER
Ralf|Schumacher|GER
Heinz-Harald|Frentzen|GER
Nick|Heidfeld|GER
Giancarlo|Fisichella|ITA
Jarno|Trulli|ITA
Alex|Zanardi|ITA
Eddie|Irvine|GBR
Johnny|Herbert|GBR
Martin|Brundle|GBR
Thierry|Boutsen|BEL
Thierry|Neuville|BEL
Stig|Blomqvist|SWE
Bjorn|Waldegard|SWE
Markku|Alen|FIN
Henri|Toivonen|FIN
Timo|Salonen|FIN
Miki|Biasion|ITA
Didier|Auriol|FRA
Stephane|Peterhansel|FRA
Richard|Burns|GBR
Mattias|Ekstrom|SWE
Andy|Priaulx|GBR
Gabriele|Tarquini|ITA
Rinaldo|Capello|ITA
Emanuele|Pirro|ITA
Romain|Dumas|FRA
Timo|Bernhard|GER
Marc|Lieb|GER
Neel|Jani|SUI
Mike|Rockenfeller|GER
Rene|Rast|GER
Antonio|Felix da Costa|POR
Mitch|Evans|NZL
Jean-Eric|Vergne|FRA
Sebastien|Bourdais|FRA
Lucas|Auer|AUT
Alexander|Wurz|AUT
Karl|Wendlinger|AUT
Christian|Klien|AUT
Dieter|Quester|AUT
Jan|Magnussen|DEN
Christian|Lundgaard|DEN
Nicki|Thiim|DEN
Marco|Sorensen|DEN
Liam|Lawson|NZL
Scott|McLaughlin|NZL
Greg|Murphy|NZL
Will|Power|AUS
Garth|Tander|AUS
Dick|Johnson|AUS
Larry|Perkins|AUS
Glenn|Seton|AUS
Chaz|Mostert|AUS
Ryan|Briscoe|AUS
Marcos|Ambrose|AUS
Mark|Winterbottom|AUS
Pedro|de la Rosa|ESP
Marc|Gene|ESP
Jaime|Alguersuari|ESP
Alex|Palou|ESP
Dani|Sordo|ESP
Antonio|Garcia|ESP
Roberto|Merhi|ESP
Stefan|Johansson|SWE
Gunnar|Nilsson|SWE
Marcus|Ericsson|SWE
Felix|Rosenqvist|SWE
Kenny|Brack|SWE
Rickard|Rydell|SWE
Jo|Bonnier|SWE
Bjorn|Wirdheim|SWE
Jos|Verstappen|NED
Jan|Lammers|NED
Gijs|van Lennep|NED
Nyck|de Vries|NED
Giedo|van der Garde|NED
Robert|Doornbos|NED
Christijan|Albers|NED
Tom|Coronel|NED
Jeroen|Bleekemolen|NED
Huub|Rothengatter|NED
Marc|Surer|SUI
Nico|Muller|SUI
Alain|Menu|SUI
Stoffel|Vandoorne|BEL
Olivier|Gendebien|BEL
Laurens|Vanthoor|BEL
Dries|Vanthoor|BEL
Maxime|Martin|BEL
Tiago|Monteiro|POR
Pedro|Lamy|POR
Filipe|Albuquerque|POR
Alvaro|Parente|POR
Vitaly|Petrov|RUS
Daniil|Kvyat|RUS
Sergey|Sirotkin|RUS
Roman|Rusinov|RUS
Artem|Markelov|RUS
Zsolt|Baumgartner|HUN
Norbert|Michelisz|HUN
Derek|Daly|IRL
Tommy|Byrne|IRL
Ralph|Firman|IRL
Craig|Breen|IRL
Henning|Solberg|NOR
Andreas|Mikkelsen|NOR
Dennis|Hauger|NOR
Kuba|Przygonski|POL
Heikki|Kovalainen|FIN
Mika|Salo|FIN
JJ|Lehto|FIN
Jari-Matti|Latvala|FIN
Tomas|Enge|CZE
Jan|Kopecky|CZE
Martin|Prokop|CZE
Jan|Charouz|CZE
Kelvin|van der Linde|RSA
Sheldon|van der Linde|RSA
Giniel|de Villiers|RSA
Sarel|van der Merwe|RSA
Ian|Scheckter|RSA
Louis|Chiron|MON
Olivier|Beretta|MON
Stefano|Coletti|MON
Arthur|Leclerc|MON
Francois|Cevert|FRA
Patrick|Depailler|FRA
Olivier|Panis|FRA
Romain|Grosjean|FRA
Henri|Pescarolo|FRA
Yannick|Dalmas|FRA
Loic|Duval|FRA
Nicolas|Prost|FRA
Lorenzo|Bandini|ITA
Elio|de Angelis|ITA
Andrea|de Cesaris|ITA
Vittorio|Brambilla|ITA
Ivan|Capelli|ITA
Alessandro|Nannini|ITA
Gianni|Morbidelli|ITA
Luca|Badoer|ITA
Vitantonio|Liuzzi|ITA
Antonio|Giovinazzi|ITA
Andrea Kimi|Antonelli|ITA
Gianmaria|Bruni|ITA
Alessandro|Pier Guidi|ITA
Antonio|Fuoco|ITA
Mirko|Bortolotti|ITA
Fabrizio|Giovanardi|ITA
Timo|Glock|GER
Adrian|Sutil|GER
Pascal|Wehrlein|GER
Mick|Schumacher|GER
Marco|Werner|GER
Wolfgang|von Trips|GER
Tony|Brooks|GBR
Peter|Collins|GBR
Paul|di Resta|GBR
Anthony|Davidson|GBR
Kris|Meeke|GBR
Elfyn|Evans|GBR
Jason|Plato|GBR
Colin|Turkington|GBR
Matt|Neal|GBR
Andy|Rouse|GBR
Tom|Chilton|GBR
Oliver|Bearman|GBR
Alex|Lynn|GBR
Sam|Bird|GBR
Jake|Dennis|GBR
Mike|Conway|GBR
Justin|Wilson|GBR
Max|Chilton|GBR
Will|Stevens|GBR
Hayden|Paddon|NZL
Possum|Bourne|NZL
David|Brabham|AUS
Vern|Schuppan|AUS
Tim|Schenken|AUS
Chris|Atkinson|AUS
Russell|Ingall|AUS
James|Courtney|AUS
Cameron|Waters|AUS
Anton|de Pasquale|AUS
"""

AMERICAS = """
Mario|Andretti|USA
Ayrton|Senna|BRA
Emerson|Fittipaldi|BRA
Nelson|Piquet|BRA
A.J.|Foyt|USA
Phil|Hill|USA
Dan|Gurney|USA
Gilles|Villeneuve|CAN
Jacques|Villeneuve|CAN
Juan Pablo|Montoya|COL
Helio|Castroneves|BRA
Jimmie|Johnson|USA
Jeff|Gordon|USA
Dale|Earnhardt|USA
Richard|Petty|USA
Rubens|Barrichello|BRA
Sergio|Perez|MEX
Al|Unser|USA
Bobby|Unser|USA
Rick|Mears|USA
Tony|Kanaan|BRA
Felipe|Massa|BRA
Josef|Newgarden|USA
Paul|Tracy|CAN
Pedro|Rodriguez|MEX
Gil|de Ferran|BRA
Michael|Andretti|USA
Bobby|Rahal|USA
Danny|Sullivan|USA
Tony|Stewart|USA
Kyle|Busch|USA
Kevin|Harvick|USA
Dale|Earnhardt Jr.|USA
Mark|Martin|USA
Ricardo|Rodriguez|MEX
Ryan|Hunter-Reay|USA
Alexander|Rossi|USA
Colton|Herta|USA
Pato|O'Ward|MEX
Cale|Yarborough|USA
David|Pearson|USA
Darrell|Waltrip|USA
Rusty|Wallace|USA
Bill|Elliott|USA
Kyle|Larson|USA
Chase|Elliott|USA
Joey|Logano|USA
Denny|Hamlin|USA
Greg|Moore|CAN
Cristiano|da Matta|BRA
Christian|Fittipaldi|BRA
Lucas|di Grassi|BRA
Hurley|Haywood|USA
Scott|Pruett|USA
Adrian|Fernandez|MEX
Graham|Rahal|USA
Marco|Andretti|USA
Lance|Stroll|CAN
Esteban|Gutierrez|MEX
Felipe|Nasr|BRA
Bruno|Senna|BRA
Nelson|Piquet Jr.|BRA
Felipe|Drugovich|BRA
Augusto|Farfus|BRA
Raul|Boesel|BRA
Mauricio|Gugelmin|BRA
Pietro|Fittipaldi|BRA
Eliseo|Salazar|CHI
Carlos|Munoz|COL
Roberto|Guerrero|COL
Sebastian|Saavedra|COL
Gabby|Chaves|COL
Tatiana|Calderon|COL
Nicholas|Latifi|CAN
Robert|Wickens|CAN
James|Hinchcliffe|CAN
Patrick|Carpentier|CAN
Scott|Goodyear|CAN
Alex|Tagliani|CAN
Ron|Fellows|CAN
Danica|Patrick|USA
Sam|Hornish Jr.|USA
Buddy|Rice|USA
Eddie|Cheever|USA
Scott|Speed|USA
Logan|Sargeant|USA
Martin|Truex Jr.|USA
Brad|Keselowski|USA
Ryan|Blaney|USA
Kurt|Busch|USA
Matt|Kenseth|USA
Bobby|Labonte|USA
Terry|Labonte|USA
Carl|Edwards|USA
Tommy|Kendall|USA
Peter|Gregg|USA
Jordan|Taylor|USA
Ricky|Taylor|USA
Joey|Hand|USA
Patrick|Long|USA
Bill|Auberlen|USA
Townsend|Bell|USA
Conor|Daly|USA
Ed|Carpenter|USA
Sage|Karam|USA
J.R.|Hildebrand|USA
Charlie|Kimball|USA
Spencer|Pigot|USA
Kyle|Kirkwood|USA
David|Malukas|USA
Santino|Ferrucci|USA
Oliver|Askew|USA
Daniel|Suarez|MEX
Benito|Guerra|MEX
Memo|Rojas|MEX
Mario|Dominguez|MEX
Michel|Jourdain Jr.|MEX
Ken|Block|USA
Travis|Pastrana|USA
Tanner|Foust|USA
Vaughn|Gittin Jr.|USA
Chris|Forsberg|USA
Ryan|Tuerck|USA
"""

def parse(block, region):
    out = []
    for line in block.strip().splitlines():
        p = [x.strip() for x in line.split('|')]
        out.append(dict(first=p[0], last=p[1], cc=p[2], eastern=len(p) > 3 and p[3] == 'E', region=region))
    return out

REGIONS = {1: parse(ASIA, 1), 2: parse(EUROPE, 2), 3: parse(AMERICAS, 3)}
