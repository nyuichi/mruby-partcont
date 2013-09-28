# mruby-partcont

partial continuation support for mruby.

## Install

```ruby
MRuby::Build.new do |conf|

    # ... (snip) ...

    conf.gem :github => 'wasabiz/mruby-partcont''
end
```

mruby-partcont does not depends on other libraries or extensions, but
for the safety it is highly recommended to add at the end of the gem build conf.

## Example

```ruby
reset {
  p 1
  shift {|k|
    p 2
	k.call()
	p 3
  }
  p 4
}
# 1
# 2
# 4
# 3
# => nil
```

## APIs

- `Continuation class`

Represents a delimited continuation that cuts the process from where
the shift operator is called up to the nearest reset operator. Since continuations
have infinite extent, once they are created it is possible to call them anytime anywhere,
and even however many times. When the continuation is cut (making a continuation closure),
the bottom of the stack of the delimited continuation is dynamically chosen.

- `Kernel.#reset { ... } -> value`
- `Kernel.#shift {|k| ... } -> value`

## ToDos

- control/prompt
- full (undelimited) continuation support
- documentation

## auther

Yuichi Nishiwaki
