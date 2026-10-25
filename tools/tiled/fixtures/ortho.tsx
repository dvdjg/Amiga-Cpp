<?xml version="1.0" encoding="UTF-8"?>
<tileset version="1.10" tiledversion="1.11.2" name="OrthoAtlas" tilewidth="16" tileheight="16" tilecount="4" columns="2">
 <grid orientation="orthogonal" width="1" height="1"/>
 <tileoffset x="1" y="-2"/>
 <image source="../../../assets/amiga/hud/test_icons.png" width="32" height="32"/>
 <properties>
  <property name="material" value="test"/>
 </properties>
 <tile id="1" class="Animated">
  <animation>
   <frame tileid="1" duration="100"/>
   <frame tileid="3" duration="150"/>
  </animation>
  <objectgroup draworder="index" id="1">
   <object id="1" x="0" y="16">
    <polygon points="0,0 16,0 16,16"/>
   </object>
  </objectgroup>
  <properties>
   <property name="solid" type="bool" value="true"/>
  </properties>
 </tile>
 <tile id="2">
  <objectgroup draworder="index" id="2">
   <object id="2" x="0" y="16">
    <polygon points="0,0 16,16 0,16"/>
   </object>
  </objectgroup>
 </tile>
</tileset>
