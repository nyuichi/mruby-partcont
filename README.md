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

## ToDos

- control/prompt
- full (undelimited) continuation support
- documentation

## auther

Yuichi Nishiwaki
