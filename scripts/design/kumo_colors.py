import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path

DECLARATION = re.compile(r"--([\w-]+)\s*:\s*(.*?);", re.DOTALL)
NAMED = {"black": "#000000", "white": "#ffffff"}


@dataclass(frozen=True)
class Oklch:
    lightness: float
    chroma: float
    hue: float | None
    alpha: float = 1.0


@dataclass(frozen=True)
class Rgb:
    red: int
    green: int
    blue: int
    alpha: float = 1.0


def top_level_split(text):
    depth, current, parts = 0, [], []
    for char in text:
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        if char == "," and depth == 0:
            parts.append("".join(current).strip())
            current = []
            continue
        current.append(char)
    parts.append("".join(current).strip())
    return parts


def number(text, percent_scale):
    if text.endswith("%"):
        return float(text[:-1]) / 100 * percent_scale
    return float(text)


def parse_hex(text):
    digits = text[1:]
    if len(digits) == 3:
        digits = "".join(char * 2 for char in digits)
    return Rgb(int(digits[0:2], 16), int(digits[2:4], 16), int(digits[4:6], 16))


def parse_color(text):
    text = " ".join(text.split())
    if text == "transparent":
        return Rgb(0, 0, 0, 0.0)
    if text in NAMED:
        return parse_hex(NAMED[text])
    if text.startswith("#"):
        return parse_hex(text)
    if text.startswith("oklch("):
        body = text[len("oklch(") : -1]
        channels, _, alpha = body.partition("/")
        lightness, chroma, hue = channels.split()
        return Oklch(
            number(lightness, 1.0),
            number(chroma, 0.4),
            None if hue == "none" else float(hue),
            number(alpha.strip(), 1.0) if alpha else 1.0,
        )
    if text.startswith("color-mix("):
        space, first, second = top_level_split(text[len("color-mix(") : -1])
        if space != "in oklch":
            raise ValueError(f"unsupported color-mix space: {space}")
        return mix(first, second)
    raise ValueError(f"unsupported color: {text}")


def weighted(part):
    color, _, weight = part.rpartition(" ")
    if weight.endswith("%"):
        return color, float(weight[:-1]) / 100
    return part, None


def as_oklch(color):
    if isinstance(color, Oklch):
        return color
    if (color.red, color.green, color.blue) == (0, 0, 0):
        return Oklch(0.0, 0.0, None, color.alpha)
    if (color.red, color.green, color.blue) == (255, 255, 255):
        return Oklch(1.0, 0.0, None, color.alpha)
    raise ValueError("color-mix supports oklch colors, black and white only")


def mix(first, second):
    first_text, first_weight = weighted(first)
    second_text, second_weight = weighted(second)
    if first_weight is None:
        first_weight = 1 - (second_weight if second_weight is not None else 0.5)
    if second_weight is None:
        second_weight = 1 - first_weight
    one = as_oklch(parse_color(first_text))
    two = as_oklch(parse_color(second_text))
    hue = one.hue if one.hue is not None else two.hue
    if one.hue is not None and two.hue is not None:
        hue = one.hue * first_weight + two.hue * second_weight
    return Oklch(
        one.lightness * first_weight + two.lightness * second_weight,
        one.chroma * first_weight + two.chroma * second_weight,
        hue,
        one.alpha * first_weight + two.alpha * second_weight,
    )


def encode(channel):
    linear = min(1.0, max(0.0, channel))
    if linear <= 0.0031308:
        value = 12.92 * linear
    else:
        value = 1.055 * linear ** (1 / 2.4) - 0.055
    return round(min(1.0, max(0.0, value)) * 255)


def to_rgb(color):
    if isinstance(color, Rgb):
        return color
    hue = math.radians(color.hue or 0.0)
    a = color.chroma * math.cos(hue)
    b = color.chroma * math.sin(hue)
    long = (color.lightness + 0.3963377774 * a + 0.2158037573 * b) ** 3
    medium = (color.lightness - 0.1055613458 * a - 0.0638541728 * b) ** 3
    short = (color.lightness - 0.0894841775 * a - 1.2914855480 * b) ** 3
    return Rgb(
        encode(4.0767416621 * long - 3.3077115913 * medium + 0.2309699292 * short),
        encode(-1.2684380046 * long + 2.6097574011 * medium - 0.3413193965 * short),
        encode(-0.0041960863 * long - 0.7034186147 * medium + 1.7076147010 * short),
        color.alpha,
    )


def hex_color(color):
    rgb = to_rgb(color)
    text = f"#{rgb.red:02x}{rgb.green:02x}{rgb.blue:02x}"
    if rgb.alpha < 1.0:
        text += f"{round(rgb.alpha * 255):02x}"
    return text


def variables(path):
    found = {}
    for name, value in DECLARATION.findall(path.read_text(encoding="utf-8")):
        found.setdefault(name, " ".join(value.split()))
    return found


def resolve(value, binding, tailwind):
    value = value.strip()
    if not value.startswith("var("):
        return value
    name, *fallback = top_level_split(value[len("var(") : -1])
    key = name.removeprefix("--")
    for source in (binding, tailwind):
        if key in source:
            return resolve(source[key], binding, tailwind)
    if not fallback:
        raise ValueError(f"unresolved variable: {name}")
    return resolve(fallback[0], binding, tailwind)


def block(text, selector):
    start = text.index("{", text.index(selector))
    depth = 0
    for index in range(start, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[start + 1 : index]
    raise ValueError(f"unterminated block after {selector}")


def token_name(variable):
    if variable.startswith("text-color-kumo-"):
        return "text-kumo-" + variable.removeprefix("text-color-kumo-")
    if variable.startswith("color-kumo-"):
        return "kumo-" + variable.removeprefix("color-kumo-")
    raise ValueError(f"not a kumo color token: {variable}")


def resolve_values(theme_path, binding_path, tailwind_path):
    text = theme_path.read_text(encoding="utf-8")
    base = text[text.index("@layer base") :]
    light = dict(DECLARATION.findall(block(base, ":root")))
    dark = dict(DECLARATION.findall(block(base, ':root[data-mode="dark"]')))
    binding = variables(binding_path)
    tailwind = variables(tailwind_path)
    return {
        token_name(name): (
            resolve(value, binding, tailwind),
            resolve(dark[name], binding, tailwind),
        )
        for name, value in light.items()
    }


def resolve_tokens(theme_path, binding_path, tailwind_path):
    return [
        (name, hex_color(parse_color(light)), hex_color(parse_color(dark)))
        for name, (light, dark) in resolve_values(
            theme_path, binding_path, tailwind_path
        ).items()
    ]


def emphasis(token):
    return [
        ("ring", f"color-mix(in oklch, {token}, black 10%)"),
        ("bg", f"color-mix(in oklch, {token}, white 30%)"),
        ("start", f"color-mix(in oklch, {token}, white 15%)"),
        ("end", token),
    ]


def emphasis_tokens(values):
    return [
        (
            f"button-emphasis-{name.removeprefix('kumo-')}-{part}",
            hex_color(parse_color(light)),
            hex_color(parse_color(dark)),
        )
        for name in ("kumo-brand", "kumo-danger")
        for (part, light), (_, dark) in zip(
            emphasis(values[name][0]), emphasis(values[name][1])
        )
    ]


def main(arguments):
    if len(arguments) != 2:
        print(
            "usage: kumo_colors.py <kumo-styles-dir> <tailwind-theme.css>",
            file=sys.stderr,
        )
        return 2
    styles = Path(arguments[0])
    paths = (styles / "theme-kumo.css", styles / "kumo-binding.css", Path(arguments[1]))
    for name, light, dark in resolve_tokens(*paths):
        print(f"{name} {light} {dark}")
    for name, light, dark in emphasis_tokens(resolve_values(*paths)):
        print(f"{name} {light} {dark}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
