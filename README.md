# GENERIC SAND

A programmable sand 'engine' that runs on the CPU. Currently loads the 4 rules supplied in "rules/".
A rule file can contain definitions for elements, along with actual rules. Which define patterns the program will identify, along with a response detailing how the program will modify the world once the pattern is identified.

The game of life could theoretically be defined with this ruleset? Problem is not every cell is activated each frame.

# Base ruleset:

The program loads 4 basic rulesets. The "sand" ruleset, "reeds" ruleset, "rainclouds" ruleset, and "fish" ruleset.

"Sand" provides rules for gravity affected elements (fallables), liquids, and elements that pile up (pileables). And provides the elements 'air', 'sand', 'stone', and 'water'. Which can be placed by using their hotkeys, ('a', 's', 'x', and 'w' respectively) and clicking somewhere on the canvas.

"Reeds" provides a seed element, and several plant element iterations. Seeds can be selected with '1'.

"Rainclouds" provides clouds, and raincloud elements. These form from a body of water and condense back into water after a certain period of time.

"Fish" provides eggs and fish. The fish is multiple elements long, but hatches from an egg in water, which can be selected with 'e'.

# Placing tips

If you want to place a single element without accidentally placing multiple, hold down the CTRL key.

You can scroll to increase/decrease the radius of painting, to fill in a larger area all at once.

TODO: Rigorously define the ruleset
----
