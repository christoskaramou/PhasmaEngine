Scripts must have the same file and class name contained: e.g. DamagedHelmet.cs -> public class DamagedHelmet

Scripts can be attached to models or they can be independent like Load.cs

To compile scripts:
	1. 	Run the _monoCmd.bat
	2. 	To output the .dll file of the target script type:
			mcs -t:library target.cs
		If the script have other script.cs dependencies add after the target.cs like:
			mcs -t:library target.cs dependency1.cs dependency2.cs dependency3.cs
		If the script have reference(.dll) dependencies add -r:dependency.dll like:
			mcs -t:library target.cs dependency1.cs -r:dependency2.dll