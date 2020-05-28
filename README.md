# Dragon Alpha 1.1.3

Note: The open source project was started many years ago and much of the notes, contact info, and resources may no longer work.  This respository is created to possibly contiue the project.

Dragon Alpha is a shareware role-playing game for Macintosh OS 8.6 and up.  Dragon Alpha runs natively under OS X.  It is a traditional RPG placed in a fantasy world.

How to build:
To build the Dragon Alpha project, first copy all the files to the desired location.  Second, go to http://developer.apple.com/sdk/ and download the latest DrawSprocket SDK.  Place the DrawSprocket SDK folder in the root Dragon Alpha Source folder and replace all old instances of it.  Next open the Dragon Alpha project and compile.  The project should compile nice and clean.

Editing Plug-Ins:
Open the "Data" file in the plug-in package in an application such as ResEdit.  Use the TMPL resource file for viewing the plug-ins data.  The "Resources Notes" file should help with some of the available values for the appropriate fields.  The ".battle", ".map", ".map2", and ".mask" are actually just PICT files.

Built-In Debug:
In the actual location of Dragon Alpha in the Dragon Alpha package, there is a file "Debug.txt" that will show up if the application experiences errors while running.  This file will contain the error result code and the function in question.

FAQs:
Q:  I've started a new game and don't know what to do.  What should I do?
A:  Your first stop should be the items in the Windmill.  Then go and buy some armor or a more powerful weapon.  Then head into the castle and go into the basement.

Q:  I'm having a hard time killing monsters what should I do?
A:  Be patient with battles, the first thing you need to do is find your enemies weakness, once that happens it's pretty easy to take them down.

Q:  Why can't I kill monsters in the forest?
A:  You really should go into the caves under the castle before venturing out into the forest.

Q:  I can't kill anyone on the mountain, help?
A:  Your not really supposed to go up into the mountains until after you've gotten pretty strong.  You need to first go into the forest and win some more powerful items.

Q:  Where are all the Dragons?
A:  Most Dragons fight you as rare fights.  They are usually stronger than your normal monster.  The odds of fighting a Dragon are usually 1 in 20.

Q:  Is there a way to quit when in battle?
A:  Yes, press q, and when it asks if you want to quit say yes.

Hints:
• Save often!!!
• Defend in battle recovers double normal recovery to both HP, magic and technique.
• There's a guard who will level you up for a price in the castle.
• There is a guard who will sell you stronger weapons on the second floor of the castle.
• Make sure to check out the Magic shop in Eleusis.  The items there can be most useful.
• Most Goblins are weak against light based attacks.
• Summons are usually found after defeating rare monsters.
• Damage done to your summons is done to you instead.

Version History:
Version 1.1.3:
-Open Sourced the Dragon Alpha project
-Game now has no registration walls
-Updated the project to use CodeWarrior for the Nib file
-Tons of minor updates for better Carbon compatibility

Version 1.1.2:
-Changed price to $12.99
-Updated for CarbonLib 1.6
-Minor Fixes and Speed improvements
-Last update unitl game changes to Dragon Arcadia

Version 1.1.0:
-Now runs in OS 8.6 or newer
-Fixed random crashes
-Fixed a bug that caused techs and magic to do less damage

Version 1.0.1:
-Fixed a bug that made a mage start with lower Mag.
-Fixed a bug that would let you recharge magics faster.
-Fixed a bug that wouldn't give monsters and summons their hit percentage bonus.
-HP on the map is now a percentage instead of actual life.
-Summons always cure the player.
-Summons always attack monsters with least life.
-Summons are now attacked less often that the player.
-Summons resistances are exteremnly increased.
-Resistances on armors and monsters are greatly decreased.
-HP are greatly increased.
-Absorbs and Magic Defense are greatly decreased.
-New game starts players on level 1 instead of level 0.
-New game heritage is adjusted to make players stronger against their primary element will decreasing their strength against their weak element.
