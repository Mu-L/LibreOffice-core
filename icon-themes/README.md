# Icon Repository for the Applications

All of the icons, separated by themes are included in this
directory. These icons are built into `.zip` files, and re-ordered `/`
packed for efficiency reasons based on our UI configuration by the
`postprocess/CustomTarget_images.mk`.

An icon theme does not need to contain all images, since these can be
layered one on top of another.

In general the layering is done like this:

    <leaf theme>
    breeze
    colibre

## Icon naming convention

sc_* icons are 16x16 px (small)
lc_* icons are 24x24 px (large)
There is also an 32px folder at cmd/ with 32 px icons

16x16 px icons are used in the menus
24x24 px icons are used in toolbars and sidebar
Notebook bar use 24x24 px icons when there is only one icon, and 16x16 px everywhere else

sc_ and lc_ icon are followed by the .uno:CommandName (see below)


## How to Add a New Image Set

- Create a directory for it here (let's call it e.g. `new_set`)

    FIXME: It is important to use an underscore `_` to delimit more words.
         `scp2` compilation crashes when using a dash `-`.
         It evidently splits the name into two strings.

    + It's probably not true anymore with filelists.
    + if this gets changed, `IconThemeSelector::SetPreferredIconTheme` needs to change too

- Add its name (`new_set`) to `WITH_THEMES` variable in `configure.ac`

- The fallback for particular icons is defined be `packimages_CUSTOM_FALLBACK_1`
  in `packimages/CustomTarget_images.mk`


## How to Add a New Icon for a New Command

- Assume you defined a dispatch command in officecfg like the following:

in `officecfg/registry/data/org/openoffice/Office/UI/CalcCommands.xcu`

    <node oor:name=".uno:OpenFromCalc" oor:op="replace">
        <prop oor:name="Label" oor:type="xs:string">
            <value xml:lang="en-US">~Open...</value>
        </prop>
        <prop oor:name="Properties" oor:type="xs:int">
            <value>1</value>
        </prop>
    </node>

  Here, you need to define a property named "Properties", with its value set
  to 1 so that the icons show up.

- Now, you need to add 2 new icon images under `icon-themes/colibre/cmd/`, one
  for the large size and one for the smaller size.  The name of each image
  must be `lc_<command name>.png` and `sc_<command name>.png`.  Here, the command
  name is the name given in the above `.xcu` file without the `.uno:` prefix and
  all its letters lower-cased.  In this example, the file names will be
  `lc_openfromcalc.png` and `sc_openfromcalc.png`.  Note that you need to add new
  images to the colibre theme for them to show up in any themes at all.

## How to Call optipng to Optimize Size

8 bit palettes are on the slow path for `quartz/svp/gtk3` so avoid using palettes with...

    $ optipng -nc <file>

## How to minify SVG files

Install https://github.com/tdewolff/minify and run

    $ minify -o outputfile.svg inputfile.svg
