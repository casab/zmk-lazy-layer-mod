# zmk-lazy-layer-mod

A [ZMK](https://zmk.dev) behavior for a key that is both a layer key and a modifier, where the
modifier only applies when the key is held **on its own**.

- Press and hold: the layer turns on at once.
- Keep holding without using another key for `delay-ms`: the modifier (for example Cmd/Win) goes
  down too, so you can Cmd+click with the mouse.
- Use any other key or layer during the hold: the modifier stays out of it for the rest of the
  hold. Keys that need it can still send it themselves, e.g. `&kp LG(C)`.
- Release without using another key: if the modifier is down it is released; if not, and
  `tap-on-release` is set, the modifier is tapped (on Windows, a Win tap opens the Start menu).

Modifier keys (Shift, Ctrl, Alt, GUI) don't count as using another key, so Cmd+Shift+click works.

## Why

A plain "layer + modifier" key holds the modifier the whole time. To type a plain symbol from that
layer, the modifier has to be released first. On Windows, releasing a Win key that no other key was
pressed with opens the Start menu, so every symbol typed from the layer opened Start. With this
behavior the modifier never wraps a key it wasn't meant for.

## Add it to your zmk-config

1. `config/west.yml`:

   ```yaml
   manifest:
     remotes:
       - name: zmkfirmware
         url-base: https://github.com/zmkfirmware
       - name: casab
         url-base: https://github.com/casab
     projects:
       - name: zmk
         remote: zmkfirmware
         revision: main
         import: app/west.yml
       - name: zmk-lazy-layer-mod
         remote: casab
         revision: main
     self:
       path: config
   ```

2. In your keymap, define the behavior and use it, usually as the hold side of a hold-tap:

   ```dts
   / {
       behaviors {
           lazy_cmd: lazy_cmd {
               compatible = "zmk,behavior-lazy-layer-mod";
               #binding-cells = <1>;          // the layer
               keycode = <RGUI>;              // modifier to hold
               delay-ms = <300>;              // held alone this long before the modifier goes down
               tap-on-release;                // released alone before that: tap the modifier
           };
           lower_cmd: lower_cmd {
               compatible = "zmk,behavior-hold-tap";
               #binding-cells = <2>;
               flavor = "tap-preferred";
               tapping-term-ms = <200>;
               bindings = <&lazy_cmd>, <&kp>;
           };
       };
   };
   ```

   Then bind `&lower_cmd 1 COMMA` (layer 1 on hold, comma on tap), or `&lazy_cmd 1` on its own.

## Properties

| Property | Type | Default | Meaning |
| --- | --- | --- | --- |
| `keycode` | int | required | The modifier, e.g. `<RGUI>`, `<LCTRL>`. |
| `delay-ms` | int | 300 | How long the key must be held alone, after the layer turns on, before the modifier goes down. |
| `tap-on-release` | bool | off | Released alone before `delay-ms`: tap the modifier. |

## Notes

- If the modifier is already down (held alone past `delay-ms`) and you then type a key that must
  not get it, that key has to drop the modifier itself, e.g. with a `zmk,behavior-mod-morph` that
  masks it. On Windows, dropping Win that way can still open the Start menu.
- Inside a hold-tap, the delay starts when the hold-tap decides "hold", so Cmd+click needs the key
  held for tapping-term + `delay-ms`.
- Only one instance is tracked at a time.

## License

MIT
