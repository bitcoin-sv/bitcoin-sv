#!/usr/bin/env python3
# Copyright (c) 2026 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Metaclass for preventing attribute typos in test framework classes.

Provides runtime validation to catch common errors like:
- 'deamon' instead of 'daemon'
- 'proces' instead of 'process'

Usage:
    from test_framework.static_attributes import StaticAttrsMeta

    class MyTestClass(metaclass=StaticAttrsMeta):
        def __init__(self):
            self.my_attribute = 123

    obj = MyTestClass()
    obj.my_attribute = 456  # Works - attribute exists
    obj.my_atribute = 789   # Raises AttributeError - typo detected!
"""


class StaticAttrsMeta(type):
    """
    Metaclass that prevents attribute typos by validating attribute names.

    After an object is initialized, only allows setting attributes that were
    created during __init__ or that exist as class attributes.
    """

    # Creates new instances of the class
    def __call__(cls, *args, **kwargs):
        instance = super().__call__(*args, **kwargs)

        valid_attrs = set()
        try:
            instance_attrs = object.__getattribute__(instance, '__dict__')
            valid_attrs.update(instance_attrs.keys())
        except AttributeError:
            pass

        for cls_item in type(instance).__mro__:
            try:
                class_attrs = object.__getattribute__(cls_item, '__dict__')
                valid_attrs.update(class_attrs.keys())
            except AttributeError:
                pass

        # Store valid attributes (using object.__setattr__ to bypass validation)
        object.__setattr__(instance, '_valid_attrs', valid_attrs)
        object.__setattr__(instance, '_initialized', True)

        return instance

    # Creates the class instance after body has executed
    def __new__(meta, clsname, bases, namespace):
        cls = super().__new__(meta, clsname, bases, namespace)

        def validated_setattr(self, attr_name, value):
            # Allow all attribute setting before initialization completes.
            # Use object.__getattribute__ to avoid triggering __getattr__
            # on classes that define it (e.g., TestNode dispatches to RPC).
            try:
                initialized = object.__getattribute__(self, '_initialized')
            except AttributeError:
                initialized = False
            if not initialized:
                return object.__setattr__(self, attr_name, value)

            # After initialization, validate the attribute name.
            # Private attributes (starting with _) are exempt because parent
            # classes like threading.Thread set internal attrs after __init__
            # (e.g., _stderr is set in Thread.start()).
            if not attr_name.startswith('_'):
                try:
                    valid_attrs = object.__getattribute__(self, '_valid_attrs')
                except AttributeError:
                    valid_attrs = set()

                if attr_name not in valid_attrs:
                    raise AttributeError(
                        f"'{type(self).__name__}' object has no attribute '{attr_name}'. "
                        f"Cannot add new attributes after initialization."
                    )

            return object.__setattr__(self, attr_name, value)

        cls.__setattr__ = validated_setattr

        return cls
