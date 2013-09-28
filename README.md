# mruby-partcont

partial continuation support for mruby.

## install

```ruby
MRuby::Build.new do |conf|

    # ... (snip) ...

    conf.gem :github => 'wasabiz/mruby-partcont''
end
```

mruby-partcont does not depends on other libraries or extensions, but
for the safety it is highly recommended to add at the end of the gem build conf.

## usage

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

## auther

Yuichi Nishiwaki
