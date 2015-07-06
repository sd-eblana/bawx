;Basic Python Application Example
Download [boxee\_basic\_python.zip](http://tapp.boxee.tv/demos/applications/boxee_basic_python_app.zip)


## descriptor.xml ##
```
<app>
  <id>boxee_basic_python_app</id>
  <name>Boxee Basic Python App</name>
  <version>1.0</version>
  <description>Demo Boxee Application (Basic)</description>
  <thumb>http://tapp.boxee.tv/apps/boxee_basic_python/thumb.png</thumb>
  <repository>http://dir.boxee.tv/apps</repository>
  <media>video</media>
  <copyright>boxee</copyright>
  <email>support@boxee.tv</email>
  <type>skin</type>
  <startWindow>start</startWindow>
  <platform>all</platform>
  <minversion>0.9.21</minversion>
  <test-app>true</test-app>
</app>
```

### start.py ###
```
import mc
import counter

mc.ShowDialogOk('Boxee Basic Python App', 'Hello! Welcome to this basic python app demo!')
if mc.ShowDialogConfirm('Boxee Basic Python App', 'Would you like to launch the application?', 'No', 'Yes!'):
	mc.ActivateWindow(14000)
```


## counter.py ##
```
import mc

play_count = 0
declined_count = 0

def updateLabel():
	global play_count, declined_count
	label = mc.GetActiveWindow().GetLabel(110)
	label.SetLabel('Play Count: %s[CR]Declined Count: %s' % (play_count, declined_count))
```


### main.xml ###
```
<?xml version="1.0"?>
<window type="window" id="14000">
   <defaultcontrol always="true">120</defaultcontrol>
   <allowoverlay>no</allowoverlay>
   <onload lang="python"><![CDATA[
counter.updateLabel()
]]></onload>
   <controls>
      <control type="group">
         <animation type="WindowOpen">
            <effect type="fade" start="0" end="100" time="200" />
         </animation>
         <control type="image">
            <width>1280</width>
            <height>720</height>
            <texture>ba_background.png</texture>
         </control>
         <control type="label" id="110">
            <posy>50</posy>
            <posx>1260</posx>
            <width>400</width>
            <align>right</align>
            <font>font21b</font>
            <color>white</color>
            <label>Play Count:0[CR]Declined Count:0</label>
         </control>
         <control type="grouplist" id="200">
             <posy>120</posy>
             <posx>30</posx>
             <ondown>120</ondown>
             <itemgap>3</itemgap>
             <orientation>horizontal</orientation>
             <control type="button" id="201">
                  <width>150</width>
                  <height>40</height>
                  <label>LOAD LIST</label>
                  <font>font21b</font>
                  <align>center</align>
                  <textoffsety>7</textoffsety>
                  <aligny>top</aligny>
                  <textcolor>ffbdbdbd</textcolor>
                  <focusedcolor>ffffffff</focusedcolor>
                  <texturefocus>ba_overlay.png</texturefocus>
                  <texturenofocus>ba_overlay2.png</texturenofocus>
                  <onclick lang="python"><![CDATA[
items = mc.GetDirectory('rss://dir.boxee.tv/apps/revision3/feed/show/filmriot/MP4-hd30')
mc.GetActiveWindow().GetList(120).SetItems(items)
]]>
</onclick>
              </control>
             <control type="button" id="202">
                  <width>150</width>
                  <height>40</height>
                  <label>CLEAR LIST</label>
                  <font>font21b</font>
                  <align>center</align>
                  <textoffsety>7</textoffsety>
                  <aligny>top</aligny>
                  <textcolor>ffbdbdbd</textcolor>
                  <focusedcolor>ffffffff</focusedcolor>
                  <texturefocus>ba_overlay.png</texturefocus>
                  <texturenofocus>ba_overlay2.png</texturenofocus>
                  <onclick lang="python"><![CDATA[
items = mc.ListItems()
mc.GetActiveWindow().GetList(120).SetItems(items)
]]>
</onclick>
              </control>
             <control type="button" id="203">
                  <width>200</width>
                  <height>40</height>
                  <label>CLEAR COUNTER</label>
                  <font>font21b</font>
                  <align>center</align>
                  <textoffsety>7</textoffsety>
                  <aligny>top</aligny>
                  <textcolor>ffbdbdbd</textcolor>
                  <focusedcolor>ffffffff</focusedcolor>
                  <texturefocus>ba_overlay.png</texturefocus>
                  <texturenofocus>ba_overlay2.png</texturenofocus>
                  <onclick lang="python"><![CDATA[
counter.play_count = 0
counter.declined_count = 0
counter.updateLabel()
]]>
</onclick>
              </control>
         </control>
         <control type="group">
            <posy>160</posy>
            <control type="image">
               <width>1280</width>
               <height>3</height>
               <texture>black.png</texture>
            </control>
            <control type="image">
               <width>1280</width>
               <height>400</height>
               <texture>ba_overlay.png</texture>
            </control>
            <control type="image">
               <posy>397</posy>
               <width>1280</width>
               <height>3</height>
               <texture>black.png</texture>
            </control>
            <control type="image">
               <posy>20</posy>
               <posx>910</posx>
               <width>355</width>
               <height>200</height>
               <info>Container(120).Listitem.thumb</info>
            </control>
            <control type="label">
               <posy>230</posy>
               <posx>1260</posx>
               <align>right</align>
               <width>500</width>
               <height>180</height>
               <info>Container(120).Listitem.property(description)</info>
               <wrapmultiline>true</wrapmultiline>
            </control>
            <control type="list" id="120">
               <posy>3</posy>
               <posx>15</posx>
               <width>700</width>
               <height>394</height>
               <onup>200</onup>
               <onleft>200</onleft>
               <scrolltime>200</scrolltime>
               <orientation>vertical</orientation>
               <itemlayout width="700" height="40">
                     <control type="label">
                        <posx>10</posx>
                        <width>680</width>
                        <height>40</height>
                        <font>font23</font>
                        <aligny>center</aligny>
                        <label>$INFO[ListItem.Label]</label>
                        <textcolor>grey</textcolor>
                     </control>
               </itemlayout>
               <focusedlayout width="600" height="40">
                  <control type="label">
                    <posx>10</posx>
                    <width>680</width>
                    <height>40</height>
                    <font>font23</font>
                    <aligny>center</aligny>
                    <label>$INFO[ListItem.Label]</label>
                    <textcolor>white</textcolor>
                    <scroll>true</scroll>
                    <visible>Control.HasFocus(120)</visible>
                  </control>
                  <control type="label">
                    <posx>10</posx>
                    <width>680</width>
                    <height>40</height>
                    <font>font23</font>
                    <aligny>center</aligny>
                    <label>$INFO[ListItem.Label]</label>
                    <textcolor>grey</textcolor>
                    <!--<scroll>true</scroll>-->
                    <visible>!Control.HasFocus(120)</visible>
                  </control>
               </focusedlayout>
               <content type="action">
                  <onclick lang="python"><![CDATA[
list = mc.GetActiveWindow().GetList(120)
item = list.GetItem(list.GetFocusedItem())
title = item.GetLabel()
confirm = mc.ShowDialogConfirm('Boxee Basic Python Application', 'Would you like to play "'+title+'"?', 'Cancel', 'Play')
if confirm:
   mc.GetPlayer().Play(item)
   counter.play_count += 1
else:
   counter.declined_count += 1
counter.updateLabel()
]]></onclick>
               </content>
            </control>
         </control>
         <control type="label">
            <posy>600</posy>
            <posx>1260</posx>
            <width>400</width>
            <align>right</align>
            <font>font21</font>
            <color>white</color>
            <label>Boxee Basic Python Application[CR]Demo[CR]Version 1.0</label>
         </control>
      </control>
   </controls>
</window>
```